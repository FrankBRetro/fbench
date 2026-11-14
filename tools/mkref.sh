#!/bin/sh
# Fbench: turn the built-in baseline profile into C.
#     sh tools/mkref.sh baseline/STE16BLT.FBP > refbase.c
# (the Makefile does this when the .FBP changes; refbase.c is committed so
# a build without sh/awk - e.g. the PC GEM one - still has it)
f="$1"
n=$(basename "$f")
echo "/* generated from $f by tools/mkref.sh - do not edit */"
echo "const char ref_file[] = \"$n\";"
echo "const char ref_fbp[] ="
tr -d '\r' < "$f" | awk '{ gsub(/\\/, "\\\\"); gsub(/"/, "\\\""); printf "    \"%s\\n\"\n", $0 }'
echo "    ;"
