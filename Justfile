build:
    cmake --build build -j

test: build
    ./build/CLAP_RT_core_test
