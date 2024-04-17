#!/bin/bash
CURDIR=$(dirname $(realpath $0))

docker buildx build -f $CURDIR/Dockerfile -t kernel_builder:0.1 .
