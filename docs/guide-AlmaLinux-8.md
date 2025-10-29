# How to compile on AlmaLiniux 8

## AlmaLinux version

```
cat /etc/redhat-release
```

| AlmaLinux release 8.10 (Cerulean Leopard)


*Note: PostgreSQL version 13 is used in these steps, adjust it based on your PostgreSQL version.*

## Setup epel, powertools, and development tools

```
sudo dnf install epel-release
sudo dnf config-manager --set-enabled powertools
sudo dnf install @"Development Tools"
```

## Setup PostgreSQL from PGDG repository

Follow steps listed here https://www.postgresql.org/download/linux/redhat/

### Install the repository RPM

```
sudo dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-8-x86_64/pgdg-redhat-repo-latest.noarch.rpm
```

### Disable the built-in PostgreSQL module

```
sudo dnf -qy module disable postgresql
```

### Install PostgreSQL 13

```
sudo dnf install -y postgresql13-server postgresql13-devel
```

### Install PostgreSQL 13 build dependencies

```
sudo dnf builddep postgresql13
```

## Add PostgreSQL 13 bin to PATH

```
export PATH=$PATH:/usr/pgsql-13/bin
```

| Also add this to your shell configuration

## Clone pgactive

```
git clone https://github.com/aws/pgactive.git
```

## Configure, Compile, and install pgactive
```
./configure
make
sudo make install
```

