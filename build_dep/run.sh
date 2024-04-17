docker run -it --rm -v $PWD:/test --entrypoint /usr/bin/bash --name kernel_builder kernel_builder:0.1 \
    -- \
    ./build_kernel.sh

docker run -it --rm -v $PWD:/test --entrypoint /usr/bin/bash --name kernel_builder kernel_builder:0.1 \
    -- \
    ./build_func_dep.sh