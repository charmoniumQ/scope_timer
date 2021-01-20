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
	  # --aspects clang_tidy/clang_tidy.bzl%clang_tidy_aspect \
	  # --output_groups=report \

# clang-tidy test/*.cpp perf_test/*.cpp -- -I.

bazel run //perf_test:cpu_timer_perf_test \
	  --cxxopt='-std=c++11' \
	  --copt='-Wall' \
	  --copt='-Wextra' \
	  --copt='-DNDEBUG' \
	  --copt='-O3' \
;
