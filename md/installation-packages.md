::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  --------------------------------------------------------------------- ---------------------------------------- ------------------------- ------------------------------------------------------------------------------
  [Prev](install-requirements.md "BDR requirements"){accesskey="P"}   [Up](installation.md){accesskey="U"}    Chapter 2. Installation    [Next](installation-source.md "Installing BDR from source"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [2.2. Installing BDR from packages]{#INSTALLATION-PACKAGES} {#installing-bdr-from-packages .SECT1}

Installation from packages is a good choice if you want the stable
release, as it\'s easier to install and to keep track of your
installation.

If you want the very latest BDR or if packages are not yet available for
your operating system you may instead want to [install from source
code](installation-source.md).

::: NOTE
> **Note:** These instructions are part of the BDR source code so they
> will be outdated if you are looking at documentation for an old BDR
> version. Installation from packages will typically install the latest
> stable BDR version.
:::

::: SECT2
## [2.2.1. RHEL, Fedora and CentOS, PostgreSQL 9.6+]{#INSTALLATION-PACKAGES-REDHAT} {#rhel-fedora-and-centos-postgresql-9.6 .SECT2}

Packages for BDR are available for Red Hat derived distros - Fedora,
RHEL, and CentOS. Packages are built for PostgreSQL 9.6+ from
yum.postgresql.org (\"PGDG\").

If you need to install or update BDR on BDR-Postgres 9.4, see
[Installing from packages on RHEL, Fedora and
CentOS](installation-packages.md#INSTALLATION-PACKAGES-REDHAT). These
instructions only apply to PostgreSQL 9.6. Confused? See [BDR
requirements](install-requirements.md).

::: SECT3
### [2.2.1.1. Install PostgreSQL 9.6 from yum.postgresql.org]{#AEN257} {#install-postgresql-9.6-from-yum.postgresql.org .SECT3}

The BDR packages only support PostgreSQL from
[yum.postgresql.org](http://yum.postgresql.org/){target="_top"}. If you
are using a different PostgreSQL distribution you will need to modify
and rebuild the packages or [install from source
code](installation-source.md).

If you do not already have PostgreSQL 9.6 from PGDG, install the PGDG
repostitory for your OS from [the repository package
list](https://yum.postgresql.org/repopackages.php){target="_top"}, then
follow the instructions to install and start PostgreSQL 9.6.

Red Hat / CentOS users should also [enable
EPEL](https://fedoraproject.org/wiki/EPEL){target="_top"} as the PGDG
repositories expect it to be available.
:::

::: SECT3
### [2.2.1.2. Install the BDR repository RPM]{#AEN266} {#install-the-bdr-repository-rpm .SECT3}

To install BDR from RPMs you should first download and install the
repository RPM for your distro. See [BDR repository
installation](https://www.2ndquadrant.com/en/resources/bdr/bdr-installation-instructions/){target="_top"}.
This RPM will configure the download location for the BDR packages and
load the signing key into your RPM database so that the package digital
signatures may be verified.

::: NOTE
> **Note:** The repository RPM is signed with 2ndQuadrant\'s master
> packaging/releases signing key. See [Verifying digital
> signatures](appendix-signatures.md).
:::
:::

::: SECT3
### [2.2.1.3. Install the BDR packages]{#AEN273} {#install-the-bdr-packages .SECT3}

To install the BDR-enabled PostgreSQL server, BDR extension, and the
client programs, simply:

``` PROGRAMLISTING
     sudo dnf check-update
     sudo dnf install postgresql-bdr96-bdr

```

Once BDR is installed, if this is a fresh PostgreSQL install you must
create a new PostgreSQL instance before proceeding, then make any
required changes to `postgresql.conf`{.FILENAME} and
`pg_hba.conf`{.FILENAME}, etc, as per any new PostgreSQl install. See
`/usr/share/doc/postgresql96/README.rpm-dist`{.FILENAME} for details.

You can then proceed with BDR-specific configuration per [Configuration
Settings](settings.md) and [Quick-start guide](quickstart.md).
:::
:::

::: SECT2
## [2.2.2. Debian or Ubuntu, PostgreSQL 9.6]{#INSTALLATION-PACKAGES-DEBIAN} {#debian-or-ubuntu-postgresql-9.6 .SECT2}

These instructions are for BDR on PostgreSQL 9.6+. For BDR-Postgres 9.4,
see [Section
2.2.5](installation-packages.md#INSTALLATION-PACKAGES-DEBIAN-94).
Confused? See [BDR requirements](install-requirements.md).

::: SECT3
### [2.2.2.1. Add the apt.postgresql.org PGDG repository and install PostgreSQL 9.6]{#AEN289} {#add-the-apt.postgresql.org-pgdg-repository-and-install-postgresql-9.6 .SECT3}

If you are not already using
[apt.postgresql.org](http://apt.postgresql.org){target="_top"} (PGDG)
PostgreSQL packages, you should install that repository and install
PostgreSQL 9.6 from there. Make sure PostgreSQL 9.6 is running normally
before proceeding with these instructions.
:::

::: SECT3
### [2.2.2.2. Add the BDR repository]{#AEN293} {#add-the-bdr-repository .SECT3}

To install BDR from DEBs you first need to add the BDR repository to
your server. See [BDR repository
installation](https://www.2ndquadrant.com/en/resources/bdr/bdr-installation-instructions/){target="_top"}.

::: NOTE
> **Note:** The package signing key is signed with 2ndQuadrant\'s master
> packaging/releases signing key. See [Verifying digital
> signatures](appendix-signatures.md).
:::
:::

::: SECT3
### [2.2.2.3. Install BDR for PostgreSQL 9.6 from packages for Debian or Ubuntu]{#AEN300} {#install-bdr-for-postgresql-9.6-from-packages-for-debian-or-ubuntu .SECT3}

BDR for PostgreSQL 9.6 is just an extension. To install it, run:

``` PROGRAMLISTING
     sudo apt-get update
     sudo apt-get install postgresql-9.6-bdr-plugin

```

Then proceed with BDR-specific configuration per [Configuration
Settings](settings.md) and [Quick-start guide](quickstart.md).
:::
:::

::: SECT2
## [2.2.3. Installing from packages on Windows]{#INSTALLATION-PACKAGES-WINDOWS} {#installing-from-packages-on-windows .SECT2}

Windows is not supported at this time. There is no major technical
barrier to doing so but it has not been a time allocation priority. See
[BDR requirements](install-requirements.md). If Windows support is
important to you, [get in touch with
2ndQuadrant](http://2ndquadrant.com/){target="_top"}.
:::

::: SECT2
## [2.2.4. Installing BDR-Postgres 9.4 RPM packages]{#INSTALLATION-PACKAGES-REDHAT-94} {#installing-bdr-postgres-9.4-rpm-packages .SECT2}

New users are encouraged to use PostgreSQL 9.6 from yum.postgresql.org
and follow the [main rpm installation
instructions](installation-packages-redhat){target="_top"}. The
following instructions are for installing BDR with BDR-Postgres 9.4, the
modified PostgreSQL 9.4 that was used by BDR 1.0. This is mainly
necessary for upgrading BDR. Confused? See [BDR
requirements](install-requirements.md).

::: SECT3
### [2.2.4.1. Install the repository RPMs]{#AEN317} {#install-the-repository-rpms .SECT3}

To install BDR from RPMs you should first download and install the
repository RPM for your distro. This RPM will configure the download
location for the BDR packages and load the signing key into your RPM
database so that the package digital signatures may be verified.

::: NOTE
> **Note:** The repository RPM is signed with 2ndQuadrant\'s master
> packaging/releases signing key. See [Verifying digital
> signatures](appendix-signatures.md).
:::

RHEL and CentOS users should download and install the appropriate repo
rpm: See [BDR repository
installation](https://www.2ndquadrant.com/en/resources/bdr/bdr-installation-instructions/){target="_top"}.

It is strongly recommended that you also enable the corresponding
repository from
[yum.postgresql.org](http://yum.postgresql.org/){target="_top"}, as the
BDR repositories only contain the BDR extension and the PostgreSQL
server, client, PLs, and the rest of the core PostgreSQL release. They
do not contain PostGIS, PgBarman, or any of the other components already
included in yum.postgresql.org releases. BDR is fully compatible with
these components.

Red Hat / CentOS users should also [enable
EPEL](https://fedoraproject.org/wiki/EPEL){target="_top"}.
:::

::: SECT3
### [2.2.4.2. Installing PostgreSQL and BDR from packages for RHEL, Fedora or CentOS]{#AEN329} {#installing-postgresql-and-bdr-from-packages-for-rhel-fedora-or-centos .SECT3}

::: SECT4
#### [2.2.4.2.1. Remove the `postgresql94`{.LITERAL} packages, if installed]{#AEN331} {#remove-the-postgresql94-packages-if-installed .SECT4}

::: NOTE
> **Note:** If you don\'t already have PostgreSQL 9.4 installed, simply
> skip this step.
:::

BDR requires a patched version of PostgreSQL 9.4, Postgres-BDR 9.4, that
conflicts with the official packages from yum.postgresql.org. If you
already have PostgreSQL 9.4 installed from yum.postgresql.org, you will
need to make a dump of all your databases, then uninstall the PGDG
PostgreSQL 9.4 packages before you can install BDR

The BDR RPMs cannot co-exist with stock PostgreSQL 9.4, and BDR does not
share the same data directory as stock 9.4, so it will not be able to
read your existing databases. (They will not be deleted, and
uninstalling BDR then reinstalling stock PGDG 9.4 will get you access to
them again, but it is strongly recommended that you dump them before
installing BDR).

Once you have fully backed up all your databases:

``` PROGRAMLISTING
      yum remove postgresql94\*

```

Check the list of packages to be removed carefully, approve the removal
if appropriate, and proceed with the removal.

Your data directory for PostgreSQL 9.4 will still exist in
`/var/lib/pgsql/9.4`{.FILENAME} but will not be used while BDR is
installed.
:::

::: SECT4
#### [2.2.4.2.2. Install the BDR packages]{#AEN342} {#install-the-bdr-packages-1 .SECT4}

To install the BDR-enabled PostgreSQL server, BDR extension, and the
client programs, simply:

``` PROGRAMLISTING
      sudo yum check-update
      sudo yum install postgresql-bdr94-bdr

```

::: NOTE
> **Note:** If you attempt to install this package when you already have
> postgresql94 installed from yum.postgresql.org, yum will report a
> conflict refuse to install it.
:::

Once BDR is installed you will need to initdb a new database, make any
required changes to `postgresql.conf`{.FILENAME} and
`pg_hba.conf`{.FILENAME}, etc, as per any new PostgreSQl install. See
`/usr/share/doc/postgresql-bdr94/README.rpm-dist`{.FILENAME} for
details.

You can then proceed with BDR-specific configuration per [Configuration
Settings](settings.md) and [Quick-start guide](quickstart.md).
:::
:::
:::

::: SECT2
## [2.2.5. Installing BDR-Postgres 9.4 and BDR for Debian/Ubuntu]{#INSTALLATION-PACKAGES-DEBIAN-94} {#installing-bdr-postgres-9.4-and-bdr-for-debianubuntu .SECT2}

New users are encouraged to use PostgreSQL 9.6 from apt.postgresql.org
and follow the [main Debian/Ubuntu installation
instructions](installation-packages.md#INSTALLATION-PACKAGES-DEBIAN).
The following instructions are for installing BDR with BDR-Postgres 9.4,
the modified PostgreSQL 9.4 that was used by BDR 1.0. This is mainly
necessary for upgrading BDR. Confused? See [BDR
requirements](install-requirements.md).

::: SECT3
### [2.2.5.1. Add the BDR repository]{#AEN360} {#add-the-bdr-repository-1 .SECT3}

To install BDR from DEBs you first need to add the BDR repository to
your server. See [BDR repository
installation](https://www.2ndquadrant.com/en/resources/bdr/bdr-installation-instructions/){target="_top"}.

Install and activate the
[apt.postgresql.org](http://apt.postgresql.org){target="_top"} (PGDG)
PostgreSQL repository. This is required by the Postgres-BDR 9.4
packages. Do [ *not*]{.emphasis} emphasis install PostgreSQL 9.4 from
apt.postgresql.org.

::: NOTE
> **Note:** The package signing key is signed with 2ndQuadrant\'s master
> packaging/releases signing key. See [Verifying digital
> signatures](appendix-signatures.md).
:::
:::

::: SECT3
### [2.2.5.2. Installing Postgres-BDR 9.4 and BDR from packages for Debian or Ubuntu]{#AEN370} {#installing-postgres-bdr-9.4-and-bdr-from-packages-for-debian-or-ubuntu .SECT3}

::: SECT4
#### [2.2.5.2.1. Remove the `postgresql-9.4`{.LITERAL} packages, if installed]{#AEN372} {#remove-the-postgresql-9.4-packages-if-installed .SECT4}

::: NOTE
> **Note:** If you don\'t already have PostgreSQL 9.4 installed, simply
> skip this step.
:::

BDR requires a patched version of PostgreSQL 9.4, Postgres-BDR 9.4, that
conflicts with the official packages. If you already have PostgreSQL 9.4
installed either from apt.postgresql.org or your official distribution
repository, you will need to make a dump of all your databases, then
uninstall the official PostgreSQL 9.4 packages before you can install
BDR.

The BDR Debian packages cannot co-exist with stock PostgreSQL 9.4. [*BDR
uses the same data directory as stock PostgreSQL 9.4 to ensure the
compatibility with system utilities, so you should always first backup
the existing instalation before trying to install the BDR PostgreSQL
packages.*]{.emphasis}.

Once you have fully backed up all your databases:

``` PROGRAMLISTING
      sudo apt-get remove postgresql-9.4

```

Check the list of packages to be removed carefully, approve the removal
if appropriate, and proceed with the removal.
:::

::: SECT4
#### [2.2.5.2.2. Install the BDR packages]{#AEN382} {#install-the-bdr-packages-2 .SECT4}

To differentiate between the BDR specific packages and vanilla
[PostgreSQL]{.PRODUCTNAME} packages all the package names start with
`postgresql-bdr`{.LITERAL} instead of plain `postgresql`{.LITERAL}. So
if you want to install the PostgreSQL package with BDR patches you
should run:

``` PROGRAMLISTING
      sudo apt-get update
      sudo apt-get install postgresql-bdr-9.4 postgresql-bdr-9.4-bdr-plugin

```

::: NOTE
> **Note:** If you attempt to install this package when you already have
> postgresql-9.4 installed you will get informed that the official
> package will be removed and confirmation will be required. [*Do not
> remove the old packages if you have existing data
> directory!*]{.emphasis}
:::

Once BDR is installed you will need to initdb a new database, make any
required changes to `postgresql.conf`{.FILENAME} and
`pg_hba.conf`{.FILENAME}, etc, as per any new PostgreSQl install. This
works with standard system utilities like `pg_createcluster`{.COMMAND}.

You can then proceed with BDR-specific configuration per [Configuration
Settings](settings.md) and [Quick-start guide](quickstart.md).
:::
:::
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  -------------------------------------------------- ---------------------------------------- -------------------------------------------------
  [Prev](install-requirements.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](installation-source.md){accesskey="N"}
  BDR requirements                                    [Up](installation.md){accesskey="U"}                         Installing BDR from source
  -------------------------------------------------- ---------------------------------------- -------------------------------------------------
:::
