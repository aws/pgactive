  [BDR 2.0.7 Documentation](README.md)                                                                                         
  [Prev](installation-source.md "Installing BDR from source")   [Up](getting-started.md)        [Next](quickstart-instances.md "Creating BDR-enabled PostgreSQL nodes/instances")  


# Chapter 3. Quick-start guide

**Table of Contents**

3.1. [Creating BDR-enabled PostgreSQL
nodes/instances](quickstart-instances.md)

3.2. [Editing the configuration files to enable
BDR](quickstart-editing.md)

3.3. [Starting the BDR-enabled PostgreSQL
nodes/instances](quickstart-starting.md)

3.4. [Creating the demo databases](quickstart-creating.md)

3.5. [Enabling BDR in SQL sessions for both of your
nodes/instances](quickstart-enabling.md)

3.6. [Testing your BDR-enabled system](quickstart-testing.md)

This section gives a quick introduction to [BDR],
including setting up a sample [BDR] installation and a few
simple examples to try. Note that this section assumes 9.6bdr and
doesn\'t apply exactly to 9.4bdr.

These instructions are not suitable for a production install, as they
neglect security considerations, proper system administration procedure,
etc. The instructions also [*assume everything is all on one
host*] so all the `pg_hba.conf` examples etc show
localhost. If you\'re trying to set up a production [BDR]
install, read the rest of the [BDR] manual, starting with
[Installation](installation.md) and [Node management
functions](functions-node-mgmt.md).

> **Note:** BDR uses [libpq connection
> strings](https://www.postgresql.org/docs/9.6/static/libpq-connect.html#LIBPQ-CONNSTRING)
> throughout. The term \"DSN\" (for \"data source name\") refers to a
> libpq connection string.

For this Quick Start example, we are setting up a two node cluster with
two PostgreSQL instances on the same server. We are using the terms node
and instance interchangeably because there\'s one node per PostgreSQL
instance in this case, and in most typical BDR setups.

To try out BDR you\'ll need to install the BDR extension. Then it\'s
necessary to [initdb] new database install(s), edit their
configuration files to load BDR, and start them up.

This quickstart guide only discusses BDR on PostgreSQL 9.6. For BDR on
patched PostgreSQL 9.4 see the main docs and install information.

Information about installing [BDR] from packages can be
found in [Installing from packages](installation-packages.md) or
installing from source can be found in [Installing from source
code](installation-source.md).



  ------------------------------------------------- ------------------------------------------- --------------------------------------------------
  [Prev](installation-source.md)        [Home](README.md)        [Next](quickstart-instances.md)  
  Installing BDR from source                         [Up](getting-started.md)     Creating BDR-enabled PostgreSQL nodes/instances
  ------------------------------------------------- ------------------------------------------- --------------------------------------------------
