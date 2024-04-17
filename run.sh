#!/bin/bash
CURDIR=$(dirname $(realpath $0))
OUTDIR=$CURDIR/output
SRCDIR=$OUTPUTDIR/src
BUILDDIR=$OUTPUT/build
KERNEL_VER="6.5" # 指定内核版本

# # 1. 构建容器
# $CURDIR/build_container/build_container.sh

# # 2. 构建内核并生成 LLVM IR 码
# docker run -it --rm -v $CURDIR:/test --name kernel_builder \
#     --env KERNEL_VER=$KERNEL_VER \
#     --entrypoint /usr/bin/bash kernel_builder:0.1 \
#     -- \
#     ./build_dep/build_kernel.sh

# 3. 基于 LLVM IR 码生成函数依赖关系
docker run -it --rm -v $CURDIR:/test  --name kernel_builder \
    --env KERNEL_VER=$KERNEL_VER \
    --entrypoint /usr/bin/bash kernel_builder:0.1 \
    -- \
    ./build_dep/build_func_dep.sh

# 4. 解析函数依赖
KERNEL_TARGET_BUILD_PATH="$BUILDDIR/linux-$KERNEL_VER-target/"
$CURDIR/build_dep/parse_dep.py $KERNEL_TARGET_BUILD_PATH $OUTDIR
