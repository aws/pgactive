# Active-active Replication Extension for PostgreSQL (pgactive)

pgactive is a PostgreSQL replication extension for creating an active-active database.

- [Documentation](./docs/)
- [Overview](#overciew)
- [Security](#security)
- [License](#license)

## Overview

Database replication is a method that copies changes between databases instances, and is a key component for use cases like high availability, reducing latency between an application and its source data, moving data between systems such as production and test, infrastructure migration, and others. Relational databases such as PostgreSQL typically support an active-standby model of replication, where one database instance accepts changes and makes them available to one or more read-only databases. Because changes in an active-standby cluster can only occur on a single instance, it’s more straightforward to build applications that work with this deployment topology because there is a single source of truth that contains all of the latest changes.

Sometimes it’s preferable to have a database cluster that uses an active-active replication topology, where it’s possible to write data on multiple instances in the same cluster. In an asynchronous active-active replication deployment, multiple databases in a cluster can accept changes and replicate them to other databases, but this means the database cluster doesn’t have a single source of truth. Use cases for this include running a Multi-Region high availability database cluster, reducing write latency between applications and databases, performing blue/green updates of applications, and migrating data between systems that must both be writable. Applications that work with active-active database clusters must be designed to handle situations that occur in this deployment topology, including conflicting changes, replication lag, and the lack of certain convenient database features such as incremental integer sequences.

A fundamental component of active-active replication is logical replication. Logical replication uses a data format that allows external systems to interpret changes before applying them to a target database. This lets the target system perform additional actions, such as detecting and resolving write conflicts or converting the statement into something that is supported in the target database software. PostgreSQL added native support for logical replication in PostgreSQL 10 in 2017, but still requires additional features to fully support an active-active replication topology. PostgreSQL’s design makes it possible to build the necessary components for supporting active-active replication in an extension while the development community continues to add it into the upstream project.

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

## License

This project is licensed under [this License](LICENSE).

