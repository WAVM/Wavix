#!/bin/sh -e

export CC_FOR_BUILD=gcc
export CC=$3
export CFLAGS="-target wasm32-unknown-wavix -mnontrapping-fptoint -fno-exceptions --sysroot $2/sys"
export AR=$4
export RANLIB=$2/bootstrap/bin/llvm-ranlib

$1/coreutils/configure --host=wasm32-unknown-wavix --prefix $2/sys
make install