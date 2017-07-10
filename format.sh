#!/bin/bash

BASEDIR=$(realpath $(dirname "$BASH_SOURCE"))
SRCDIR=$BASEDIR/src

find $SRCDIR -iname '*.cpp' -o -iname '*.h' -o -iname '*.c' |while read fname; do
  clang-format -i $fname
done

