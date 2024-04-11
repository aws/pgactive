# User Guide

## Public Functions

### get\_last\_applied\_xact\_info

Gets last applied transaction info of apply worker for a given node.

### pgactive\_apply\_pause

Pause applying replication.

### pgactive\_apply\_resume

Resume applying replication.

### pgactive\_is\_apply\_paused

Chewck if replication apply is paused.

### pgactive\_create\_group

Create a pgactive group, turning a stand-alone database into the first node in a pgactive group.

### pgactive\_detach\_nodes

Detach node(s) from pgactive group.

### pgactive\_get\_connection\_replication\_sets

Get replication sets for the given node.

### pgactive\_get\_replication\_lag\_info

Gets replication lag info.

### pgactive\_get\_stats

Get pgactive replication stats.

### pgactive\_join\_group

Join an existing pgactive group by connecting to a member node and copying its contents.

### pgactive\_remove

Remove all traces of pgactive from the local node.

### pgactive\_snowflake\_id\_nextval

Generate sequence values unique to this node using a local sequence as a seed

### pgactive\_update\_node\_conninfo

Update pgactive node connection info.

### Internal Functions

These internal functions are not recommended for general use.

### check\_file\_system\_mount\_points

Checks if given two paths are on same file system mount points.

### get\_free\_disk\_space

Gets free disk space in bytes of filesystem to which given path is mounted.

### has\_required\_privs

Checks if current user has required privileges.

### pgactive\_acquire\_global\_lock

TBD

### pgactive\_assign\_seq\_ids\_post\_upgrade

TBD

### pgactive\_connections\_changed

Function to notify other background info to refresh connectiob.

### pgactive\_conninfo\_cmp

Checks if given two connectgions are same.

### pgactive\_create\_conflict\_handler 

TBD

### pgactive\_drop\_conflict\_handler

TBD

### pgactive\_fdw\_validator

TBD

### pgactive\_format\_replident\_name

TBD

### pgactive\_format\_slot\_name

TBD

### pgactive\_get\_connection\_replication\_sets

TBD

### pgactive\_get\_connection\_replication\_sets

TBD

### pgactive\_get\_last\_applied\_xact\_info

TBD

### pgactive\_get\_local\_node\_name

TBD

### pgactive\_get\_local\_nodeid

TBD

### pgactive\_get\_node\_identifier

TBD

### pgactive\_get\_table\_replication\_sets

TBD

### pgactive\_get\_workers\_info

TBD

### pgactive\_handle\_rejoin

TBD

### pgactive\_internal\_create\_truncate\_trigger

TBD

### pgactive\_is\_active\_in\_db

TBD

### pgactive\_min\_remote\_version\_num

TBD

### pgactive\_node\_status\_from\_char

TBD

### pgactive\_node\_status\_to\_char

TBD

### pgactive\_parse\_replident\_name

TBD

### pgactive\_parse\_slot\_name

TBD

### pgactive\_queue\_truncate

TBD

### pgactive\_replicate\_ddl\_command

TBD

### pgactive\_set\_connection\_replication\_sets

TBD

### pgactive\_set\_node\_read\_only

TBD

### pgactive\_set\_table\_replication\_sets

TBD

### pgactive\_skip\_changes

TBD

### pgactive\_terminate\_workers

TBD

### pgactive\_truncate\_trigger\_add

TBD

### pgactive\_variant

TBD

### pgactive\_version

TBD

### pgactive\_version\_num

TBD

### pgactive\_wait\_for\_node\_ready

TBD

### pgactive\_wait\_for\_slots\_confirmed\_flush\_lsn

TBD

### pgactive\_xact\_replication\_origin

TBD

## Private Functions

These private functions are not recommended for general use.

### \_pgactive\_begin\_join\_private

### \_pgactive\_begin\_join\_private

### \_pgactive\_check\_file\_system\_mount\_points

### \_pgactive\_destroy\_temporary\_dump\_directories\_private

### \_pgactive\_generate\_node\_identifier\_private

### \_pgactive\_get\_free\_disk\_space

### \_pgactive\_get\_node\_info\_private

### \_pgactive\_has\_required\_privs

### \_pgactive\_join\_node\_private

### \_pgactive\_nid\_shmem\_reset\_all\_private

### \_pgactive\_pause\_worker\_management\_private

### \_pgactive\_snowflake\_id\_nextval\_private

### \_pgactive\_update\_seclabel\_private

