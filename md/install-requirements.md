  [BDR 2.0.7 Documentation](README.md)                                                                                        
  [Prev](installation.md "Installation")   [Up](installation.md)    Chapter 2. Installation    [Next](installation-packages.md "Installing BDR from packages")  


# 2.1. BDR requirements

BDR runs on unmodified PostgreSQL 9.6 as distributed by the PostgreSQL
Development Group (PGDG), i.e. postgresql.org. The source code will also
work with any largely unmodified re-distribution like those by Ubuntu,
Red Hat, etc, though 2ndQuadrant only provides packages for PGDG
releases.

BDR is also compatible with BDR-Postgres 9.4, a modified PostgreSQL 9.4
that was required by BDR 1.x releases. New installs should use
PostgreSQL 9.6. Support for BDR-Postgres 9.4 is mainly for existing
deployments and for upgrades. See [Upgrading BDR](upgrade.md)\> and
[BDR requirements
(9.4)](install-requirements.md#INSTALL-REQUIREMENTS-94).

PostgreSQL 9.5 is not supported and will never be supported. Nor is
unmodified community PostgreSQL 9.4, or any older version of PostgreSQL.

As of the time of writing, Microsoft Windows is not yet a supported
platform for BDR. Support may be added in later releases. If Windows
support is important to you, check [BDR
website](http://2ndquadrant.com/BDR) for the latest
information.

## 2.1.1. BDR Requirements (BDR-Postgres 9.4)

To run on PostgreSQL 9.4, BDR requires a modified PostgreSQL called
BDR-Postgres 9.4. The modified PostgreSQL adds functionality BDR needs
to support DDL replication, Active-Active conflict resolution,
user-defined conflict handlers, etc. It is available from the BDR
download page alongside the extension in both package and source form.
This requirement means that [*you can\'t use BDR on unmodified
PostgreSQL 9.4*].

Some of these modifications make small changes to PostgreSQL data
directory. As a result the modified PostgreSQL 9.4 used by BDR can\'t
load data directories from unmodified PostgreSQL and vice versa. Users
must dump and reload their database(s) to switch to a BDR-capable
PostgreSQL 9.4. See [Installation](installation.md).

This is not the case for BDR running on PostgreSQL 9.6, where no
modified PostgreSQL is required.



  ------------------------------------------ ---------------------------------------- ---------------------------------------------------
  [Prev](installation.md)      [Home](README.md)       [Next](installation-packages.md)  
  Installation                                [Up](installation.md)                         Installing BDR from packages
  ------------------------------------------ ---------------------------------------- ---------------------------------------------------
