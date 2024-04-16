docker run -it --rm -v $PWD:/test --name kernel_builder kernel_builder:0.1 \
    -- \
    ./build_kernel.sh