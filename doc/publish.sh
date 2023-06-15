#!/bin/sh

set -e -u

rev=$(awk '/^#define BDR_VERSION / { match($3,"\"([0-9]+\\.[0-9]+)\\.[0-9]+",a); print a[1] }' ../bdr_version.h)

(cd .. && autoreconf && ./config.status --recheck)

MAKE=${MAKE:-make}
DOCHOST="bdr-project.org"
DOCDIR="/var/www_bdr-project.org/docs/"

if [ -z "$rev" ]; then
  echo "Couldn't determine version"
  exit 1
else
  echo "Building docs for bdr-plugin $rev"
fi

$MAKE clean
rm -rf html
$MAKE JADEFLAGS="-V website-build"

echo "Suppressing BDR 2.0 from website other versions list"
touch html/.hide_version

echo -n "Uploading... "
RSYNC_RSH="ssh -o StrictHostKeyChecking=no" rsync \
  -r --delete html/ \
  $DOCHOST:$DOCDIR/$rev/
echo "done"
