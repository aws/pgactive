::: NAVHEADER
  [BDR 2.0.6 Documentation](index.md)                                                                    
  ------------------------------------------------------------------ ---------------------------------- -- ------------------------------------------------------------------------------------
  [Prev](conflicts-logging.md "Conflict logging"){accesskey="P"}   [Up](manual.md){accesskey="U"}        [Next](global-sequences-purpose.md "Purpose of global sequences"){accesskey="N"}

------------------------------------------------------------------------
:::

::: CHAPTER
# []{#GLOBAL-SEQUENCES}Chapter 10. Global Sequences

::: TOC
**Table of Contents**

10.1. [Purpose of global sequences](global-sequences-purpose.md)

10.2. [When to use global sequences](global-sequences-when.md)

10.3. [Using global sequences](global-sequence-usage.md)

10.4. [Global sequence limitations](global-sequence-limitations.md)

10.5. [Global sequences and ORMs](global-sequences-orms.md)

10.6. [Global sequence voting](global-sequence-voting.md)

10.7. [Traditional approaches to sequences in distributed
DBs](global-sequences-alternatives.md)

10.7.1. [Step/offset
sequences](global-sequences-alternatives.md#GLOBAL-SEQUENCES-ALTERNATIVE-STEPOFFSET)

10.7.2. [Composite
keys](global-sequences-alternatives.md#GLOBAL-SEQUENCES-ALTERNATIVE-COMPOSITE)

10.7.3.
[UUIDs](global-sequences-alternatives.md#GLOBAL-SEQUENCES-ALTERNATIVE-UUID)

10.8. [BDR 1.0 global sequences](global-sequences-bdr10.md)
:::

BDR global sequences provide an easy way for applications to use the
database to generate unique synthetic keys in an asynchronous
distributed system.

::: IMPORTANT
> **Important:** This chapter refers to the global sequences
> implementation in BDR 2.0 and newer. See the BDR 1.0 documentation and
> the upgrade guide for details on the quite different global sequences
> implementation in BDR 1.0.
:::

::: WARNING
  -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  **Warning**
  Object-relational mappers and applications that hardcode calls to the `nextval`{.LITERAL} function may require special configuration to work with BDR 2.0 global sequences.
  -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
:::
:::

::: NAVFOOTER

------------------------------------------------------------------------

  ----------------------------------------------- ----------------------------------- ------------------------------------------------------
  [Prev](conflicts-logging.md){accesskey="P"}    [Home](index.md){accesskey="H"}    [Next](global-sequences-purpose.md){accesskey="N"}
  Conflict logging                                 [Up](manual.md){accesskey="U"}                              Purpose of global sequences
  ----------------------------------------------- ----------------------------------- ------------------------------------------------------
:::
