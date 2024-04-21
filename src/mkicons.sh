#!/bin/sh

# Copyright (C) 2022-2024 alx@fastestcode.org
# This software is distributed under the terms of the X/MIT license.
# See the included COPYING file for further information.

# Generates a header file containing pixmaps for built-in icons and some
# convenience C data structures to access these (see graphics.c)

ICONS="file dir nxdir mpt mpti dlink text bin"

if [ -z "$1" ]; then
	TGT=icons.h
else
	TGT="$1"
fi

SIZES="t s m l"

printf '/* Generated by mkicons.sh */\n\n' > $TGT
printf 'struct xpm_data { char *name; char **data; };\n' >> $TGT

for n in $ICONS; do
	for s in $SIZES; do
		cat "icons/$n.$s.xpm" | sed -e 's/_xpm//g' >> $TGT
	done
done

printf "\nstatic struct xpm_data xpm_images[] = {" >> $TGT
for n in $ICONS; do
	for s in $SIZES; do
		printf "\n{ \"%s\", %s }," "${n}.$s"  "${n}_$s" >> $TGT
	done
done

printf "\n};\n" >> $TGT

printf "static const unsigned int num_xpm_images =
	sizeof(xpm_images) / sizeof(struct xpm_data);\n" >> $TGT
