#!/bin/bash
# 执行 callgraph
KERNEL_VER=6.5
CURDIR=$(dirname $(realpath $0))
BUILDDIR="$CURDIR/build/"

KERNEL_TARGET_BUILD_PATH="$BUILDDIR/linux-$KERNEL_VER-target/"
TARGET_BCLIST="$CURDIR/tmp/target-allbc.list"

pushd ./pass
mkdir build

pushd build
cmake ..
make -j$(nproc)
popd

find $KERNEL_TARGET_BUILD_PATH -name "*.bc" > $TARGET_BCLIST
./build/callgraph -f $TARGET_BCLIST
popd