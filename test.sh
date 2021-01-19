#!/usr/bin/env sh
bazel test //test:cpu_timer_test --cxxopt='-std=c++11' --cxxopt='-Wall' --cxxopt='-Wextra' --cxxopt='-Werror'
