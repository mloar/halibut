#!/bin/sh 

# Make a Halibut release archive.

RELDIR="$1"
VERSION="$2"

linkmirror() {
    (cd "$1"; find . -name CVS -prune -o -name .svn -prune -o \
     -name build -prune -o -name reltmp -prune -o -type d -print) | \
     while read dir; do mkdir -p "$2"/"$dir"; done
    (cd "$1"; find . -name CVS -prune -o -name .svn -prune -o \
     -name build -prune -o -name reltmp -prune -o \
     -name '*.orig' -prune -o -name '*.rej' -prune -o \
     -name '*.txt' -prune -o -name '*.html' -prune -o \
     -name '*.1' -prune -o -name '.cvsignore' -prune -o \
     -name '*.gz' -prune -o -name '.[^.]*' -prune -o \
     -type f -print) | \
     while read file; do ln -s "$1"/"$file" "$2"/"$file"; done
}

linkmirror $PWD reltmp/$RELDIR
if ! test -d charset; then
    linkmirror $PWD/../charset reltmp/$RELDIR/charset
fi

if test "x${VERSION}y" != "xy"; then
    (cd reltmp/$RELDIR;
     find . -name '*.[ch]' -exec md5sum {} \;
     ) > reltmp/$RELDIR/manifest
    echo "-DVERSION=\"${VERSION}\"" > reltmp/$RELDIR/version;
fi

tar chzvoCf reltmp $RELDIR.tar.gz $RELDIR

rm -rf reltmp
