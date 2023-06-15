::: NAVHEADER
  [BDR 2.0.7 Documentation](index.md)
  --------------------------------------------------------------------------------------- -------------------------------------------- ------------------------------ -----------------------------------------------------------------------------
  [Prev](global-sequence-limitations.md "Global sequence limitations"){accesskey="P"}   [Up](global-sequences.md){accesskey="U"}    Chapter 10. Global Sequences    [Next](global-sequence-voting.md "Global sequence voting"){accesskey="N"}

------------------------------------------------------------------------
:::

::: SECT1
# [10.5. Global sequences and ORMs]{#GLOBAL-SEQUENCES-ORMS} {#global-sequences-and-orms .SECT1}

Some applications and ORM (Object-Relational Mapper) tools expect to
call `nextval`{.LITERAL} on a sequence directly, rather than using the
table\'s declared `DEFAULT`{.LITERAL} for a column. If such an ORM is
configured to recognise a sequence default for a column (like
JPA/Hibernate\'s `@SequenceGenerator`{.LITERAL}) or auto-detects it
(like Rails/ActiveRecord\'s default `id`{.LITERAL} magic) and assumes
that it can get new values with `nextval(...)`{.LITERAL} without
checking the table\'s actual `DEFAULT`{.LITERAL}, it will produce values
using the underlying sequence not the BDR global sequence. This will
very likely result in conflicts.You\'ll have to change/override this, or
get them to fetch generated IDs from the database after insert, which
most support. Also many ORMs assume that nextval will return some \'n\'
values, like an increment of 50. They can\'t do that with global
sequences. Make sure their increment is 1.

Application developers should configure these tools to fetch the
database-generated default instead of using a sequence cache, or to call
the correct `bdr.global_seq_nextval`{.FUNCTION} function to generate a
value. The application may not assume a block of IDs has been generated,
as if an increment was set, and may only use the actual value returned.

Alternately, the DBA may choose to create a new schema (say
`bdr_seq`{.LITERAL}), add wrapper functions for
`nextval(regclass)`{.LITERAL} and `nextval(text)`{.LITERAL} there that
call `bdr.global_seq_nextva(regclass)`{.LITERAL}, and ensure that the
application\'s `search_path`{.LITERAL} puts the new schema
`bdr_seq`{.LITERAL} [*before*]{.emphasis} `pg_catalog`{.LITERAL}.
:::

::: NAVFOOTER

------------------------------------------------------------------------

  --------------------------------------------------------- -------------------------------------------- ----------------------------------------------------
  [Prev](global-sequence-limitations.md){accesskey="P"}        [Home](index.md){accesskey="H"}         [Next](global-sequence-voting.md){accesskey="N"}
  Global sequence limitations                                [Up](global-sequences.md){accesskey="U"}                                Global sequence voting
  --------------------------------------------------------- -------------------------------------------- ----------------------------------------------------
:::
