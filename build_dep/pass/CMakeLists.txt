cmake_minimum_required (VERSION 3.13.4)
project (CallgraphPass)

find_package(LLVM REQUIRED CONFIG)

add_executable (callgraph buildcallgraph.cpp)

target_include_directories (callgraph PUBLIC
    "${LLVM_INCLUDE_DIRS}")
target_link_directories (callgraph PUBLIC
    "${LLVM_LIB_DIRS}")
target_compile_options (callgraph PRIVATE
    "-D_GLIBCXX_USE_CXX11_ABI=1"
    )
target_link_libraries (callgraph LINK_PUBLIC
    LLVMCore
    LLVMSupport
    LLVMIRReader
    )
