  [BDR 2.0.7 Documentation](README.md)                                                                                                                               
  [Prev](global-sequence-limitations.md "Global sequence limitations")   [Up](global-sequences.md)    Chapter 10. Global Sequences    [Next](global-sequence-voting.md "Global sequence voting")  


# [10.5. Global sequences and ORMs]

Some applications and ORM (Object-Relational Mapper) tools expect to
call `nextval` on a sequence directly, rather than using the
table\'s declared `DEFAULT` for a column. If such an ORM is
configured to recognise a sequence default for a column (like
JPA/Hibernate\'s `@SequenceGenerator`) or auto-detects it
(like Rails/ActiveRecord\'s default `id` magic) and assumes
that it can get new values with `nextval(...)` without
checking the table\'s actual `DEFAULT`, it will produce values
using the underlying sequence not the BDR global sequence. This will
very likely result in conflicts.You\'ll have to change/override this, or
get them to fetch generated IDs from the database after insert, which
most support. Also many ORMs assume that nextval will return some \'n\'
values, like an increment of 50. They can\'t do that with global
sequences. Make sure their increment is 1.

Application developers should configure these tools to fetch the
database-generated default instead of using a sequence cache, or to call
the correct `bdr.global_seq_nextval` function to generate a
value. The application may not assume a block of IDs has been generated,
as if an increment was set, and may only use the actual value returned.

Alternately, the DBA may choose to create a new schema (say
`bdr_seq`), add wrapper functions for
`nextval(regclass)` and `nextval(text)` there that
call `bdr.global_seq_nextva(regclass)`, and ensure that the
application\'s `search_path` puts the new schema
`bdr_seq` [*before*] `pg_catalog`.



  --------------------------------------------------------- -------------------------------------------- ----------------------------------------------------
  [Prev](global-sequence-limitations.md)        [Home](README.md)         [Next](global-sequence-voting.md)  
  Global sequence limitations                                [Up](global-sequences.md)                                Global sequence voting
  --------------------------------------------------------- -------------------------------------------- ----------------------------------------------------
