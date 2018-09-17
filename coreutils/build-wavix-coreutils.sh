#!/bin/sh -e

export CC_FOR_BUILD=gcc
export CFLAGS="-target wasm32-unknown-wavix -mnontrapping-fptoint -fno-exceptions --sysroot $2/sys"
export CC=$4
export AR=$5
export RANLIB=$6
export CPP=$7
export CPPFLAGS=-I$2/sys/include
export YACC=

$1/coreutils/configure --host=wasm32-unknown-wavix --prefix $3/sys
make install
rm -rf $1/coreutils/autom4te.cache