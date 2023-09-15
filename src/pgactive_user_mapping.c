/* -------------------------------------------------------------------------
 *
 * pgactive_user_mapping.c
 *		FOREIGN SERVER and USER MAPPING implementation for pgactive
 *
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		pgactive_user_mapping.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgactive_compat.h"
#include "pgactive_internal.h"

#include "access/reloptions.h"
#include "access/xact.h"
#include "catalog/pg_foreign_data_wrapper.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_user_mapping.h"
#include "executor/spi.h"
#include "foreign/foreign.h"
#include "libpq-fe.h"
#include "miscadmin.h"
#include "parser/scansup.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/snapmgr.h"

static char *escape_param_str(const char *from);
static bool is_valid_dsn_option(const PQconninfoOption *options,
								const char *option, Oid context);

/*
 * Parse user mapping info with a string containing key=value pairs.
 *
 * Core logic for this function is taken from conninfo_parse() in
 * src/interfaces/libpq/fe-connect.c.
 */
static void
user_mapping_parse(const char *usermappinginfo,
				   char **usermapping,
				   char **foreignserver)
{
	char	   *pname;
	char	   *pval;
	char	   *buf;
	char	   *cp;
	char	   *cp2;

	/* Need a modifiable copy of the input string */
	buf = pstrdup(usermappinginfo);
	cp = buf;

	while (*cp)
	{
		/* Skip blanks before the parameter name */
		if (isspace((unsigned char) *cp))
		{
			cp++;
			continue;
		}

		/* Get the parameter name */
		pname = cp;
		while (*cp)
		{
			if (*cp == '=')
				break;
			if (isspace((unsigned char) *cp))
			{
				*cp++ = '\0';
				while (*cp)
				{
					if (!isspace((unsigned char) *cp))
						break;
					cp++;
				}
				break;
			}
			cp++;
		}

		/* Check that there is a following '=' */
		if (*cp != '=')
		{
			/* syntax error in list */
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_NAME),
					 errmsg("missing \"=\" after \"%s\" in user mapping info string",
							pname),
					 errhint("Valid user mapping info string looks like user_mapping=<<name>> pgactive_foreign_server=<<name>>.")));

		}
		*cp++ = '\0';

		/* Skip blanks after the '=' */
		while (*cp)
		{
			if (!isspace((unsigned char) *cp))
				break;
			cp++;
		}

		/* Get the parameter value */
		pval = cp;

		if (*cp != '\'')
		{
			cp2 = pval;
			while (*cp)
			{
				if (isspace((unsigned char) *cp))
				{
					*cp++ = '\0';
					break;
				}
				if (*cp == '\\')
				{
					cp++;
					if (*cp != '\0')
						*cp2++ = *cp++;
				}
				else
					*cp2++ = *cp++;
			}
			*cp2 = '\0';
		}
		else
		{
			cp2 = pval;
			cp++;
			for (;;)
			{
				if (*cp == '\0')
				{
					/* syntax error in list */
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_NAME),
							 errmsg("unterminated quoted string in user mapping info string"),
							 errhint("Valid user mapping info string looks like user_mapping=<<name>> pgactive_foreign_server=<<name>>.")));

				}
				if (*cp == '\\')
				{
					cp++;
					if (*cp != '\0')
						*cp2++ = *cp++;
					continue;
				}
				if (*cp == '\'')
				{
					*cp2 = '\0';
					cp++;
					break;
				}
				*cp2++ = *cp++;
			}
		}

		/*
		 * Now that we have the name and the value, store the record.
		 */
		if (pg_strncasecmp(pname, "user_mapping", 12) == 0)
		{
			*usermapping = pstrdup(pval);
			truncate_identifier(*usermapping, strlen(*usermapping), true);
		}
		else if (pg_strncasecmp(pname, "pgactive_foreign_server", 23) == 0)
		{
			*foreignserver = pstrdup(pval);
			truncate_identifier(*foreignserver, strlen(*foreignserver), true);
		}
		else
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_NAME),
					 errmsg("invalid parameter name \"%s\" specified in user mapping info string",
							pname),
					 errhint("Valid user mapping info string looks like user_mapping=<<name>> pgactive_foreign_server=<<name>>.")));
	}

	/* Done with the modifiable input string */
	pfree(buf);
}

/*
 * Function taken from contrib/dblink/dblink.c
 *
 * Return value is a palloc, caller must free it if needed.
 *
 * Obtain connection string for a given foreign server and user mapping.
 */
char *
get_connect_string(const char *usermappinginfo)
{
	ForeignServer *foreign_server = NULL;
	UserMapping *user_mapping;
	Oid			serverid;
	Oid			fdwid;
	ListCell   *cell;
	StringInfoData buf;
	ForeignDataWrapper *fdw;
	AclResult	aclresult;
	bool		tx_started = false;
	const PQconninfoOption *options = NULL;
	char	   *umname = NULL;
	char	   *fsname = NULL;
	StringInfoData cmd;
	Oid			umuser;
	PQconninfoOption *opts = NULL;

	/*
	 * First check if it's a valid connection string, if yes, do nothing
	 * because it's not user mapping info.
	 */
	opts = PQconninfoParse(usermappinginfo, NULL);
	if (opts != NULL)
		return NULL;

	initStringInfo(&buf);

	/*
	 * Get list of valid libpq options.
	 *
	 * To avoid unnecessary work, we get the list once and use it throughout
	 * the lifetime of this backend process.  We don't need to care about
	 * memory context issues, because PQconndefaults allocates with malloc.
	 */
	if (!options)
	{
		options = PQconndefaults();
		if (!options)			/* assume reason for failure is OOM */
			ereport(ERROR,
					(errcode(ERRCODE_FDW_OUT_OF_MEMORY),
					 errmsg("out of memory"),
					 errdetail("Could not get libpq's default connection options.")));
	}
	/* first parse and gather the user mapping info options */
	user_mapping_parse(usermappinginfo, &umname, &fsname);

	if (!IsTransactionState())
	{
		tx_started = true;
		StartTransactionCommand();
	}

	foreign_server = GetForeignServerByName(fsname, false);

	initStringInfo(&cmd);

	appendStringInfo(&cmd, "SELECT umuser FROM pg_catalog.pg_user_mappings "
					 "WHERE usename = '%s' AND srvname = '%s';",
					 umname, fsname);

	if (SPI_connect() != SPI_OK_CONNECT)
		elog(ERROR, "SPI_connect failed");
	PushActiveSnapshot(GetTransactionSnapshot());

	if (SPI_execute(cmd.data, false, 0) != SPI_OK_SELECT)
		elog(ERROR, "SPI_execute failed: %s", cmd.data);

	if (SPI_processed != 1 || SPI_tuptable->tupdesc->natts != 1)
	{
		elog(FATAL, "could not fetch umuser from pg_catalog.pg_user_mappings: got %d rows and %d columns, expected 1 row and 1 column",
			 (int) SPI_processed, SPI_tuptable->tupdesc->natts);
	}

	umuser = DatumGetObjectId(
							  DirectFunctionCall1(oidin,
												  CStringGetDatum(SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1))));

	if (SPI_finish() != SPI_OK_FINISH)
		elog(ERROR, "SPI_finish failed");
	PopActiveSnapshot();

	serverid = foreign_server->serverid;
	fdwid = foreign_server->fdwid;

	user_mapping = GetUserMapping(umuser, serverid);
	fdw = GetForeignDataWrapper(fdwid);

	/* Check permissions, user must have usage on the server. */
	aclresult = pg_foreign_server_aclcheck(serverid, umuser, ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, OBJECT_FOREIGN_SERVER, foreign_server->servername);

	foreach(cell, fdw->options)
	{
		DefElem    *def = lfirst(cell);

		if (is_valid_dsn_option(options, def->defname, ForeignDataWrapperRelationId))
			appendStringInfo(&buf, "%s='%s' ", def->defname,
							 escape_param_str(strVal(def->arg)));
	}

	foreach(cell, foreign_server->options)
	{
		DefElem    *def = lfirst(cell);

		if (is_valid_dsn_option(options, def->defname, ForeignServerRelationId))
			appendStringInfo(&buf, "%s='%s' ", def->defname,
							 escape_param_str(strVal(def->arg)));
	}

	foreach(cell, user_mapping->options)
	{

		DefElem    *def = lfirst(cell);

		if (is_valid_dsn_option(options, def->defname, UserMappingRelationId))
			appendStringInfo(&buf, "%s='%s' ", def->defname,
							 escape_param_str(strVal(def->arg)));
	}

	if (tx_started)
		CommitTransactionCommand();

	return buf.data;
}

/*
 * Function taken from contrib/dblink/dblink.c
 *
 * Escaping libpq connect parameter strings.
 *
 * Return value is a palloc, caller must free it if needed
 *
 * Replaces "'" with "\'" and "\" with "\\".
 */
char *
escape_param_str(const char *str)
{
	const char *cp;
	StringInfoData buf;

	initStringInfo(&buf);

	for (cp = str; *cp; cp++)
	{
		if (*cp == '\\' || *cp == '\'')
			appendStringInfoChar(&buf, '\\');
		appendStringInfoChar(&buf, *cp);
	}

	return buf.data;
}

/*
 * Functions taken from contrib/dblink/dblink.c
 *
 * Check if the specified connection option is valid.
 *
 * We basically allow whatever libpq thinks is an option, with these
 * restrictions:
 *		debug options: disallowed
 *		"client_encoding": disallowed
 *		"user": valid only in USER MAPPING options
 *		secure options (eg password): valid only in USER MAPPING options
 *		others: valid only in FOREIGN SERVER options
 *
 * We disallow client_encoding because it would be overridden anyway via
 * PQclientEncoding; allowing it to be specified would merely promote
 * confusion.
 */
bool
is_valid_dsn_option(const PQconninfoOption *options, const char *option,
					Oid context)
{
	const PQconninfoOption *opt;

	/* Look up the option in libpq result */
	for (opt = options; opt->keyword; opt++)
	{
		if (strcmp(opt->keyword, option) == 0)
			break;
	}
	if (opt->keyword == NULL)
		return false;

	/* Disallow debug options (particularly "replication") */
	if (strchr(opt->dispchar, 'D'))
		return false;

	/* Disallow "client_encoding" */
	if (strcmp(opt->keyword, "client_encoding") == 0)
		return false;

	/*
	 * If the option is "user" or marked secure, it should be specified only
	 * in USER MAPPING.  Others should be specified only in SERVER.
	 */
	if (strcmp(opt->keyword, "user") == 0 || strchr(opt->dispchar, '*'))
	{
		if (context != UserMappingRelationId)
			return false;
	}
	else
	{
		if (context != ForeignServerRelationId)
			return false;
	}

	return true;
}

/*
 * Functions taken from contrib/dblink/dblink.c
 *
 * Validate the options given to a pgactive foreign server or user mapping.
 * Raise an error if any option is invalid.
 *
 * We just check the names of options here, so semantic errors in options,
 * such as invalid numeric format, will be detected at the attempt to connect.
 */
PG_FUNCTION_INFO_V1(pgactive_fdw_validator);
Datum
pgactive_fdw_validator(PG_FUNCTION_ARGS)
{
	List	   *options_list = untransformRelOptions(PG_GETARG_DATUM(0));
	Oid			context = PG_GETARG_OID(1);
	ListCell   *cell;

	static const PQconninfoOption *options = NULL;

	/*
	 * Get list of valid libpq options.
	 *
	 * To avoid unnecessary work, we get the list once and use it throughout
	 * the lifetime of this backend process.  We don't need to care about
	 * memory context issues, because PQconndefaults allocates with malloc.
	 */
	if (!options)
	{
		options = PQconndefaults();
		if (!options)			/* assume reason for failure is OOM */
			ereport(ERROR,
					(errcode(ERRCODE_FDW_OUT_OF_MEMORY),
					 errmsg("out of memory"),
					 errdetail("Could not get libpq's default connection options.")));
	}

	/* Validate each supplied option. */
	foreach(cell, options_list)
	{
		DefElem    *def = (DefElem *) lfirst(cell);

		if (!is_valid_dsn_option(options, def->defname, context))
		{
			/*
			 * Unknown option, or invalid option for the context specified, so
			 * complain about it.  Provide a hint with list of valid options
			 * for the context.
			 */
			StringInfoData buf;
			const PQconninfoOption *opt;

			initStringInfo(&buf);
			for (opt = options; opt->keyword; opt++)
			{
				if (is_valid_dsn_option(options, opt->keyword, context))
					appendStringInfo(&buf, "%s%s",
									 (buf.len > 0) ? ", " : "",
									 opt->keyword);
			}
			ereport(ERROR,
					(errcode(ERRCODE_FDW_OPTION_NAME_NOT_FOUND),
					 errmsg("invalid option \"%s\"", def->defname),
					 buf.len > 0
					 ? errhint("Valid options in this context are: %s",
							   buf.data)
					 : errhint("There are no valid options in this context.")));
		}
	}

	PG_RETURN_VOID();
}
