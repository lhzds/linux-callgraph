#!/bin/bash
CURDIR=$(dirname $(realpath $0))
SRCDIR="$CURDIR/src"
BUILDDIR="$CURDIR/build/"
KERNEL_VER="6.5"

KERNEL_SRC_PATH="$SRCDIR/linux-$KERNEL_VER/"
KERNEL_TARGET_BUILD_PATH="$BUILDDIR/linux-$KERNEL_VER-target/"

mkdir -p $SRCDIR
mkdir -p $KERNEL_TARGET_BUILD_PATH

# 1. 下载源码
if [ ! -d "$KERNEL_SRC_PATH" ]; then
  wget -c https://cdn.kernel.org/pub/linux/kernel/v${KERNEL_VER:0:1}.x/linux-$KERNEL_VER.tar.xz -O - | tar -xJ -C $SRCDIR
fi
if [ "${KERNEL_VER:0:1}" -lt "5" ] || [ "${KERNEL_VER:0:1}" = "5" ] && [ "${KERNEL_VER:2:2}" -le "15" ]; then
#  https://lore.kernel.org/r/20211104215047.663411-1-nathan@kernel.org/
  sed -i 's/1E6L/USEC_PER_SEC/g' $KERNEL_SRC_PATH/drivers/power/reset/ltc2952-poweroff.c
#  https://lore.kernel.org/r/20211104215923.719785-1-nathan@kernel.org/
  sed -i 's/1E6L/USEC_PER_SEC/g' $KERNEL_SRC_PATH/drivers/usb/dwc2/hcd_queue.c
# gcc sanitizer bug
  sed -i 's/-Wall//g' $KERNEL_SRC_PATH/tools/lib/subcmd/Makefile
# TODO: use patch instead of sed
fi

# 2. 编译内核
pushd $KERNEL_SRC_PATH
make mrproper
make CC=clang defconfig O=$KERNEL_TARGET_BUILD_PATH
make CC=clang -j$(nproc) KCFLAGS='-w' vmlinux modules O=$KERNEL_TARGET_BUILD_PATH
make CC=clang -j$(nproc) INSTALL_MOD_PATH=./mod_install modules_install O=$KERNEL_TARGET_BUILD_PATH
popd

# 3. 重新编译生成 .bc 文件
${CURDIR}/buildir.py $KERNEL_TARGET_BUILD_PATH

