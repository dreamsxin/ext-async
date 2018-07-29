#!/usr/bin/env bash

DIR=$(readlink -f ./thirdparty)

if [[ -f "$DIR/lib/libuv.a" ]]; then
  echo "Libuv has already been compiled"
  exit 0
fi

TMP=$(mktemp -d)

if [[ ! "$TMP" || ! -d "$TMP" ]]; then
  echo "Failed to create libuv temp directory"
  exit 1
fi

trap "rm -rf $TMP" EXIT

# Build libuv:
cp -r ./thirdparty/libuv $TMP/

pushd $TMP/libuv
./autogen.sh
./configure --prefix=$TMP/build CFLAGS="$(CFLAGS) -fPIC -DPIC -g -O2"
make
make install
popd

cp $TMP/build/lib/libuv.a $DIR/lib/
