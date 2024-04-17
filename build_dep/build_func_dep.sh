#!/bin/bash
CURDIR=$(dirname $(realpath $0)) 
ROOTDIR=$(dirname $(dirname $(realpath $0)))

OUTDIR=$ROOTDIR/output
BUILDDIR=$OUTDIR/build

KERNEL_TARGET_BUILD_PATH="$BUILDDIR/linux-$KERNEL_VER-target/"
TARGET_BCLIST="$OUTDIR/target-allbc.list"

pushd $CURDIR/pass

mkdir build
pushd build
cmake ..
make -j$(nproc)
popd

find $KERNEL_TARGET_BUILD_PATH -name "*.bc" > $TARGET_BCLIST
$CURDIR/build/callgraph -f $TARGET_BCLIST

popd