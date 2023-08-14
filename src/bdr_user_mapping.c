/* -------------------------------------------------------------------------
 *
 * bdr_user_mapping.c
 *		FOREIGN SERVER and USER MAPPING implementation for BDR
 *
 *
 * Copyright (C) 2012-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		bdr_user_mapping.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "bdr_compat.h"
#include "bdr_internal.h"

#include "access/reloptions.h"
#include "access/xact.h"
#include "catalog/pg_foreign_data_wrapper.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_user_mapping.h"
#include "foreign/foreign.h"
#include "libpq-fe.h"
#include "miscadmin.h"
#include "parser/scansup.h"
#include "utils/acl.h"

static char *escape_param_str(const char *from);
static bool is_valid_dsn_option(const PQconninfoOption *options,
								const char *option, Oid context);

/*
 * Function taken from contrib/dblink/dblink.c
 *
 * Return value is a palloc, caller must free it if needed
 *
 * Obtain connection string for a foreign server
 */
char *
get_connect_string(const char *servername)
{
	ForeignServer *foreign_server = NULL;
	UserMapping *user_mapping;
	ListCell   *cell;
	StringInfoData buf;
	ForeignDataWrapper *fdw;
	AclResult	aclresult;
	char	   *srvname;
	bool		CloseTransaction = false;
	const PQconninfoOption *options = NULL;

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
	/* first gather the server connstr options */
	srvname = pstrdup(servername);
	truncate_identifier(srvname, strlen(srvname), false);

	if (!IsTransactionState())
	{
		StartTransactionCommand();
		CloseTransaction = true;
	}

	foreign_server = GetForeignServerByName(srvname, true);
	if (foreign_server)
	{
		Oid			serverid = foreign_server->serverid;
		Oid			fdwid = foreign_server->fdwid;
		Oid			userid = GetUserId();

		user_mapping = GetUserMapping(userid, serverid);
		fdw = GetForeignDataWrapper(fdwid);

		/* Check permissions, user must have usage on the server. */
		aclresult = pg_foreign_server_aclcheck(serverid, userid, ACL_USAGE);
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

		if (CloseTransaction)
			CommitTransactionCommand();

		return buf.data;
	}
	else
	{
		if (CloseTransaction)
			CommitTransactionCommand();
		return NULL;
	}
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
 * Validate the options given to a bdr foreign server or user mapping.
 * Raise an error if any option is invalid.
 *
 * We just check the names of options here, so semantic errors in options,
 * such as invalid numeric format, will be detected at the attempt to connect.
 */
PG_FUNCTION_INFO_V1(bdr_fdw_validator);
Datum
bdr_fdw_validator(PG_FUNCTION_ARGS)
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
