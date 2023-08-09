  [BDR 2.1.0 Documentation](README.md)                                                                    
  [Prev](conflicts-logging.md "Conflict logging")   [Up](manual.md)        [Next](global-sequences-purpose.md "Purpose of global sequences")  


# Chapter 10. Global Sequences

**Table of Contents**

10.1. [Purpose of global sequences](global-sequences-purpose.md)

10.2. [When to use global sequences](global-sequences-when.md)

10.3. [Using global sequences](global-sequence-usage.md)

10.4. [Global sequence limitations](global-sequence-limitations.md)

10.5. [Global sequences and ORMs](global-sequences-orms.md)

10.6. [Traditional approaches to sequences in distributed
DBs](global-sequences-alternatives.md)

10.6.1. [Step/offset
sequences](global-sequences-alternatives.md#GLOBAL-SEQUENCES-ALTERNATIVE-STEPOFFSET)

10.6.2. [Composite
keys](global-sequences-alternatives.md#GLOBAL-SEQUENCES-ALTERNATIVE-COMPOSITE)

10.6.3.
[UUIDs](global-sequences-alternatives.md#GLOBAL-SEQUENCES-ALTERNATIVE-UUID)

BDR global sequences provide an easy way for applications to use the
database to generate unique synthetic keys in an asynchronous
distributed system.

  **Warning**
  Object-relational mappers and applications that hardcode calls to the `nextval` function may require special configuration to work with BDR 2.0 global sequences.



  ----------------------------------------------- ----------------------------------- ------------------------------------------------------
  [Prev](conflicts-logging.md)    [Home](README.md)    [Next](global-sequences-purpose.md)  
  Conflict logging                                 [Up](manual.md)                              Purpose of global sequences
  ----------------------------------------------- ----------------------------------- ------------------------------------------------------
