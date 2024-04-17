#!/bin/bash
CURDIR=$(dirname $(realpath $0))
ROOTDIR=$(dirname $(dirname $(realpath $0)))
BUILDDIR="$ROOTDIR/build_dep/build"
KERNEL_VER="6.5"
KERNEL_TARGET_BUILD_PATH="$BUILDDIR/linux-$KERNEL_VER-target/"

./parse_dep.py $KERNEL_TARGET_BUILD_PATH