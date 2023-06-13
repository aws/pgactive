::: BOOK
[]{#BDR}

::: TITLEPAGE
# [BDR 2.0.7 Documentation]{#BDR} {#bdr-2.0.7-documentation .TITLE}

### 2ndQuadrant Ltd {#ndquadrant-ltd .CORPAUTHOR}

[Copyright](LEGALNOTICE.md) © 1996-2016 The PostgreSQL Global
Development Group

<div>

::: ABSTRACT
[]{#AEN23}

This book is the official documentation of BDR 2.0.7 for use with
PostgreSQL 9.6 or with a modified version of PostgreSQL 9.4. It has been
written by the [PostgreSQL]{.PRODUCTNAME} and BDR developers and other
volunteers in parallel to the development of the BDR software. It
describes all the functionality that the current version of BDR
officially supports.

BDR was developed by
[2ndQuadrant](http://2ndquadrant.com) along with
contributions from other individuals and companies. Contributions from
the community are appreciated and welcome - get in touch via
[github](http://github.com/2ndQuadrant/bdr) or [the
mailing
list/forum](https://groups.google.com/a/2ndquadrant.com/forum/#!forum/bdr-list).
Multiple 2ndQuadrant customers contribute funding to make BDR
development possible.

2ndQuadrant, a Platinum sponsor of the PostgreSQL project, continues to
develop BDR to meet internal needs and those of customers. 2ndQuadrant
is also working actively with the PostgreSQL community to integrate BDR
into PostgreSQL. Other companies as well as individual developers are
welcome to participate in the efforts.

Multiple technologies emerging from BDR development have already become
integral part of core PostgreSQL, such as [Event
Triggers](https://www.postgresql.org/docs/current/static/event-triggers.html),
[Logical
Decoding](https://www.postgresql.org/docs/current/static/logicaldecoding.html),
[Replication
Slots](https://www.postgresql.org/docs/current/static/logicaldecoding-explanation.html#LOGICALDECODING-REPLICATION-SLOTS),
[Background
Workers](https://www.postgresql.org/docs/current/static/bgworker.html),
[Commit
Timestamps](https://wiki.postgresql.org/wiki/What's_new_in_PostgreSQL_9.5#Commit_timestamp_tracking),
[Replication
Origins](https://www.postgresql.org/docs/9.5/static/replication-origins.html),
[DDL event
capture](https://www.postgresql.org/docs/9.5/static/functions-event-triggers.html#PG-EVENT-TRIGGER-DDL-COMMAND-END-FUNCTIONS),
[generic WAL messages for logical
decoding](https://www.postgresql.org/docs/9.6/static/functions-admin.html#FUNCTIONS-REPLICATION-TABLE).
:::

</div>

------------------------------------------------------------------------
:::

::: TOC
**Table of Contents**

I. [Getting started](getting-started.md)

1\. [BDR overview](overview.md)

2\. [Installation](installation.md)

3\. [Quick-start guide](quickstart.md)

II\. [BDR administration manual](manual.md)

4\. [Configuration Settings](settings.md)

5\. [Node Management](node-management.md)

6\. [Command-line Utilities](commands.md)

7\. [Monitoring](monitoring.md)

8\. [DDL Replication](ddl-replication.md)

9\. [Multi-master conflicts](conflicts.md)

10\. [Global Sequences](global-sequences.md)

11\. [Replication Sets](replication-sets.md)

12\. [Functions](functions.md)

13\. [Catalogs and Views](catalogs-views.md)

14\. [Upgrading [BDR]{.PRODUCTNAME}](upgrade.md)

A. [Release notes](releasenotes.md)

A.1. [Release 2.0.5](release-2.0.5.md)

A.2. [Release 2.0.4](release-2.0.4.md)

A.3. [Release 2.0.3](release-2.0.3.md)

A.4. [Release 2.0.2](release-2.0.2.md)

A.5. [Release 2.0.1](release-2.0.1.md)

A.6. [Release 2.0.0](release-2.0.0.md)

A.7. [Release 1.0.2](release-1.0.2.md)

A.8. [Release 1.0.1](release-1.0.1.md)

A.9. [Release 1.0.0](release-1.0.0.md)

A.10. [Release 0.9.3](release-0.9.3.md)

A.11. [Release 0.9.2](release-0.9.2.md)

A.12. [Release 0.9.1](release-0.9.1.md)

A.13. [Release 0.9.0](release-0.9.0.md)

A.14. [Release 0.8.0](release-0.8.0.md)

A.15. [Release 0.7.0](release-0.7.md)

B. [Verifying digital signatures](appendix-signatures.md)

C. [Technical notes](technotes.md)

C.1. [BDR network structure](technotes-mesh.md)

C.2. [DDL locking details](technotes-ddl-locking.md)

C.3. [Full table rewrites](technotes-rewrites.md)

[Index](bookindex.md)
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --- --- ---------------------------------------------
            [Next](getting-started.md){accesskey="N"}
                                        Getting started
  --- --- ---------------------------------------------
:::
