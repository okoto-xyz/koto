#!/bin/bash

export LC_ALL=C
TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

ZCASHD=${ZCASHD:-$SRCDIR/kotod}
ZCASHCLI=${ZCASHCLI:-$SRCDIR/koto-cli}
ZCASHTX=${ZCASHTX:-$SRCDIR/koto-tx}

[ ! -x $ZCASHD ] && echo "$ZCASHD not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
read -r -a ZECVERSTR <<< "$($ZCASHCLI --version | head -n1 | awk '{ print $NF }')"
read -r -a ZECVER <<< "$(echo $ZECVERSTR | awk -F- '{ OFS="-"; NF--; print $0; }')"
read -r -a ZECCOMMIT <<< "$(echo $ZECVERSTR | awk -F- '{ print $NF }')"

# Create a footer file with copyright content.
# This gets autodetected fine for zcashd if --version-string is not set,
# but has different outcomes for zcash-cli.
echo "[COPYRIGHT]" > footer.h2m
$ZCASHD --version | sed -n '1!p' >> footer.h2m

for cmd in $ZCASHD $ZCASHCLI $ZCASHTX; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=$ZECVER --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-$ZECCOMMIT//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
