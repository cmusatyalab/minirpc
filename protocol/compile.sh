#!/bin/sh

warn="AUTOGENERATED FILE -- DO NOT EDIT"
warn_c="/* $warn */"
warn_make="# $warn"

# cond_cat src dest str
# Prepend $str to file $src, and if the contents are identical to the contents
# of file $dest, delete $src.  Otherwise replace $dest with $src.
cond_cat() {
	src=$1
	dest=$2
	str=$3
	# No need to rebuild if the actual content of $dest hasn't changed
	if [ ! -f $dest ] || ! cat $dest | fgrep -v "$str" | cmp -s $src -
	then
		echo "$str" > $dest
		cat $src >> $dest
	fi
	rm -f $src
}

if [ "$ASN1C" = "" ] ; then
	ASN1C=asn1c
fi
err=0
$ASN1C 2>/dev/null || err=$?
if [ $err = 127 ] ; then
	echo -n "Error: Can't find asn1c.  Try adding it to your PATH or " >&2
	echo -e "setting the ASN1C\nenvironment variable." >&2
	exit 1
fi

tempfile="output.$$"
classrules="classes/classes.$$"
supportrules="support/support.$$"
trap "rm -f $tempfile $classrules $supportrules" EXIT

$ASN1C -Werror -fskeletons-copy $* > $tempfile 2>&1
result=$?
egrep -v "^(Copied|Generated|Compiled)" $tempfile >&2
if [ $result != 0 ] ; then
	exit $result
fi

set -e

rm -f Makefile.am.sample converter-sample.c
generated=`awk '/^Compiled/ {print $2}' $tempfile`
copied=`awk '/^Copied/ {print $NF}' $tempfile`

echo "CLASS_FILES = " > $classrules
for file in $generated
do
	cond_cat "$file" "classes/$file" "$warn_c"
	echo "CLASS_FILES += $file" >> $classrules
done
cond_cat $classrules classes/classes.mk "$warn_make"

echo "SUPPORT_FILES = " > $supportrules
for file in $copied
do
	# Filter out converter-sample.c
	[ -f "$file" ] || continue
	cond_cat "$file" "support/$file" "$warn_c"
	echo "SUPPORT_FILES += $file" >> $supportrules
done
cond_cat $supportrules support/support.mk "$warn_make"

touch stamp
