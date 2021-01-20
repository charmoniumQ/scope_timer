#!/bin/bash
set -e

bazel test //test:cpu_timer_test \
				--cxxopt='-std=c++11' \
				--copt='-Wall' \
				--copt='-Wextra' \
				--copt='-fsanitize=address' \
				--copt='-Og' \
				--copt='-g' \
				--linkopt='-fsanitize=address' \
				--strip=never \
|| (cat bazel-out/k8-fastbuild/testlogs/test/cpu_timer_test/test.log ; exit 1)

set -o noglob
checks='*'

clang-tidy test/test_cpu_timer.cpp -- -I.
