  [BDR 2.0.7 Documentation](README.md)                                                                                            
  ----------------------------------------------------------- ---------------------------------------- --------------------------- -----------------------------------------------------------
  [Prev](release-2.0.0.md "Release 2.0.0")   [Up](releasenotes.md)    Appendix A. Release notes    [Next](release-1.0.1.md "Release 1.0.1")  


# A.7. Release 1.0.2

[*Mon Nov 14, 2016*]

The BDR 1.0.2 maintenance release fixes an intermittent failure in
[bdr_init_copy] caused by failure to wait until promotion
of a copied node has fully completed. (#255).

Also fixes failure to replicate the `ch_timeframe` argument to
peers when user defined conflict handlers are created.

Alongside the bdr-plugin 1.0.2 release, BDR-Postgres is updated to
9.4.10, including all fixes and improvements from PostgreSQL 9.4.10. See
[the PostgreSQL 9.4.10 release
notes](https://www.postgresql.org/docs/9.4/static/release-9-4-10.html).



  ------------------------------------------- ---------------------------------------- -------------------------------------------
  [Prev](release-2.0.0.md)      [Home](README.md)       [Next](release-1.0.1.md)  
  Release 2.0.0                                [Up](releasenotes.md)                                Release 1.0.1
  ------------------------------------------- ---------------------------------------- -------------------------------------------
