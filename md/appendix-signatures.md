  [BDR 2.0.7 Documentation](README.md)                                                            
  --------------------------------------------------------- ----------------------------------- -- ---------------------------------------------------------
  [Prev](release-0.7.md "Release 0.7.0")   [Home](README.md)        [Next](technotes.md "Technical notes")  


# []{#APPENDIX-SIGNATURES}Appendix B. Verifying digital signatures

The signing key ID used for source code and RPM releases of BDR versions
based on PostgreSQL 9.4 is [the key
`0x6E192B0E`](http://packages.2ndquadrant.com/postgresql-bdr94-2ndquadrant/RPM-GPG-KEY-2NDQ-BDR-94).

To download the BDR 9.4 RPM repository key to your computer:

``` PROGRAMLISTING
   curl -s http://packages.2ndquadrant.com/postgresql-bdr94-2ndquadrant/RPM-GPG-KEY-2NDQ-BDR-94 | gpg --import
   gpg --fingerprint 0x6E192B0E
  
```

then verify that the fingerprint is the expected value:

``` PROGRAMLISTING
   9793 74C1 0580 940E 9611  1BE3 A879 B734 6E19 2B0E
  
```

The BDR / 9.4 RPM releases key is in turn signed by [the 2ndQuadrant
master packaging/releases key with key ID
`0x2B11E054`](http://packages.2ndquadrant.com/2NDQUADRANT-PACKAGING-MASTER-KEY.asc).
You can [verify the fingerprint of the master packaging key on the
2ndQuadrant
website.](http://2ndquadrant.com/signing-keys)

For checking tarballs, download the BDR release signing key as shown
above, then use [gpg] directly to verify, e.g.:

``` PROGRAMLISTING
   gpg --verify bdr-0.8.0.tar.gz.asc
  
```

To check a repository RPM, use [rpmkeys] to load the
master packaging signing key into the RPM database then use
`rpm -K`, e.g.:

``` PROGRAMLISTING
   sudo rpmkeys --import http://packages.2ndquadrant.com/2NDQUADRANT-PACKAGING-MASTER-KEY.asc
   rpm -K postgresql-bdr94-2ndquadrant-redhat-1.0-2.noarch.rpm
  
```

If you want to manually verify individual RPMs you will need to load the
repository key. This is usually installed for you when you install the
repository RPM, then [yum] checks the package signatures
after download. So it is [*not*] typically necessary to
manually verify signatures so long as you verified the repository RPM.

``` PROGRAMLISTING
   sudo rpmkeys --import http://packages.2ndquadrant.com/postgresql-bdr94-2ndquadrant/RPM-GPG-KEY-2NDQ-BDR-94
   rpm -K some-bdr-rpm.rpm
  
```

The packaging master key also signs the repository key for the Debian
and Ubuntu packages. The current repository key ID for the apt
repository is `0xAA7A6805` and can be downloaded from [the
repository
site](http://packages.2ndquadrant.com/bdr/apt/AA7A6805.asc).



  ----------------------------------------- ----------------------------------- ---------------------------------------
  [Prev](release-0.7.md)    [Home](README.md)    [Next](technotes.md)  
  Release 0.7.0                                                                                         Technical notes
  ----------------------------------------- ----------------------------------- ---------------------------------------
