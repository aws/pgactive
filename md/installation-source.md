::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  ---------------------------------------------------------------------------------- ---------------------------------------- ------------------------- ------------------------------------------------------------
  [Prev](installation-packages.md "Installing BDR from packages"){accesskey="P"}   [Up](installation.md){accesskey="U"}    Chapter 2. Installation    [Next](quickstart.md "Quick-start guide"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [2.3. Installing BDR from source]{#INSTALLATION-SOURCE} {#installing-bdr-from-source .SECT1}

::: SECT2
## [2.3.1. Installation from source for Postgres-BDR 9.6]{#INSTALLATION-SOURCE-EXTENSION} {#installation-from-source-for-postgres-bdr-9.6 .SECT2}

This section discusses installing BDR on PostgreSQL 9.6+. For
instructions on installing BDR-Postgres 9.4 and BDR for 9.4, see
[Section 2.3.2.3](installation-source.md#INSTALLATION-BDR-SOURCE-94).
Confused? See [BDR requirements](install-requirements.md).

::: SECT3
### [2.3.1.1. Prerequisites for installing from source]{#INSTALLATION-SOURCE-PREREQS} {#prerequisites-for-installing-from-source .SECT3}

To install BDR on PostgreSQL 9.6, you need PostgreSQL 9.6 installed. If
you installed PostgreSQL from packages (either distributor packages or
postgresql.org packages) you will also generally need a -dev or -devel
package, the name of which depends on your OS and which PostgreSQL
packages you installed.

For [apt.postgresql.org](http://apt.postgresql.org/)
packages, install `postgresql-server-dev-9.6`{.LITERAL}. For
[yum.postgresql.org](http://yum.postgresql.org) packages,
install `postgresql96-devel`{.LITERAL}. (Or just [install BDR from
packages](installation-packages.md)). For other package origins, see
their documentation; you need the package that contains
`postgres.h`{.FILENAME} and `pg_config`{.FILENAME}.
:::

::: SECT3
### [2.3.1.2. Getting BDR source code]{#INSTALLATION-GET-SOURCE} {#getting-bdr-source-code .SECT3}

Source code can be obtained by unpacking release source tarballs or
clone from git. See
[http://2ndquadrant.com/bdr](http://2ndquadrant.com/bdr)
for more information.
:::

::: SECT3
### [2.3.1.3. Installation of BDR for PostgreSQL 9.6 from source]{#INSTALLATION-BDR-SOURCE} {#installation-of-bdr-for-postgresql-9.6-from-source .SECT3}

To add the BDR 2.0 extension to your PostgreSQL 9.6 install execute its
configure script with the [pg_config]{.APPLICATION} from PostgreSQL 9.6
in the `PATH`{.LITERAL} environment variable, e.g.:

``` PROGRAMLISTING
     cd /path/to/bdr-plugin-source/
     PATH=/path/to/postgres96/install/bin:"$PATH" ./configure
     make -j4 -s all
     make -s install

```
:::
:::

::: SECT2
## [2.3.2. Installation from source for Postgres-BDR 9.4]{#INSTALLATION-SOURCE-94} {#installation-from-source-for-postgres-bdr-9.4 .SECT2}

This section discusses installing BDR and BDR-Postgres 9.4. This is
mainly useful and necessary for users upgrading from BDR 1.0. New BDR
users should prefer to [install BDR as an extension to PostgreSQL
9.6](installation-source.md#INSTALLATION-SOURCE-EXTENSION).

::: SECT3
### [2.3.2.1. Prerequisites for installing from source]{#INSTALLATION-SOURCE-PREREQS-94} {#prerequisites-for-installing-from-source-1 .SECT3}

To install Postgres-BDR 9.4 and the BDR extension the prerequisites for
compiling PostgreSQL must be installed. These are described in
PostgreSQL\'s documentation on [build
requirements](http://www.postgresql.org/docs/current/install-requirements.html)
and [build requirements for
documentation](http://www.postgresql.org/docs/current/docguide-toolsets.html).

On several systems the prerequisites for compiling Postgres-BDR and the
BDR extension can be installed using simple commands.

-   `Debian`{.LITERAL} and `Ubuntu`{.LITERAL}: First add the
    [apt.postgresql.org](http://apt.postgresql.org/)
    repository to your `sources.list`{.FILENAME} if you have not already
    done so. Then install the pre-requisites for building PostgreSQL
    with:

    ``` PROGRAMLISTING
       sudo apt-get update
        sudo apt-get build-dep postgresql-9.4

    ```

-   `RHEL or CentOS 6.x or 7.x`{.LITERAL}: install the appropriate
    repository RPM for your system from
    [yum.postgresql.org](http://yum.postgresql.org/repopackages.php).
    Then install the prerequisites for building PostgreSQL with:

    ``` PROGRAMLISTING
       sudo yum check-update
        sudo yum groupinstall "Development Tools"
        sudo yum install yum-utils openjade docbook-dtds docbook-style-dsssl docbook-style-xsl
           sudo yum-builddep postgresql94

    ```
:::

::: SECT3
### [2.3.2.2. Getting BDR source code]{#INSTALLATION-GET-SOURCE-94} {#getting-bdr-source-code-1 .SECT3}

Source code can be obtained by unpacking release source tarballs or
clone from git. See
[http://2ndquadrant.com/bdr](http://2ndquadrant.com/bdr)
for more information.
:::

::: SECT3
### [2.3.2.3. Installation of BDR for Postgres-BDR 9.4 from source]{#INSTALLATION-BDR-SOURCE-94} {#installation-of-bdr-for-postgres-bdr-9.4-from-source .SECT3}

Installing BDR for 9.4 from source consists out of two steps: First
compile and install Postgres-BDR 9.4; secondly compile and install the
BDR plugin.

The patched PostgreSQL 9.4 required for BDR on 9.4 can be compiled using
the [normal documented
procedures](http://www.postgresql.org/docs/current/static/installation.html).
That will usually be something like:

``` PROGRAMLISTING
    cd /path/to/bdr-pg-source/
    ./configure --prefix=/path/to/install --enable-debug --with-openssl
    make -j4 -s install-world

```

To then install BDR execute its configure script with the
[pg_config]{.APPLICATION} installed by the patched PostgreSQL in the
`PATH`{.LITERAL} environment variable, e.g.:

``` PROGRAMLISTING
    cd /path/to/bdr-plugin-source/
    PATH=/path/to/postgres/install/bin:"$PATH" ./configure
    make -j4 -s all
    make -s install

```
:::
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------------------- ---------------------------------------- ----------------------------------------
  [Prev](installation-packages.md){accesskey="P"}      [Home](index.md){accesskey="H"}       [Next](quickstart.md){accesskey="N"}
  Installing BDR from packages                         [Up](installation.md){accesskey="U"}                         Quick-start guide
  --------------------------------------------------- ---------------------------------------- ----------------------------------------
:::
