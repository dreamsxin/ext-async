#!/usr/bin/env bash

# First argument determines location:
DIR=${1}

# Prepare tmp build dir:
rm -rf /tmp/ext-async
mkdir /tmp/ext-async
cp -r ./thirdparty/libuv /tmp/ext-async/

# Build libuv:
pushd /tmp/ext-async/libuv
./autogen.sh

if test "$DIR" != ''; then
  ./configure --prefix=$(readlink -f $DIR)
else
  ./configure
fi

make
make install
popd

# Cleanup
rm -rf /tmp/ext-async
