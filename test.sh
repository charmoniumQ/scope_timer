#!/bin/bash
set -e

bazel test //test:cpu_timer_test \
				--cxxopt='-std=c++11' \
				--cxxopt='-Wall' \
				--cxxopt='-Wextra' \
				--cxxopt='-fsanitize=address' \
				--cxxopt='-Og' \
				--cxxopt='-g' \
				--cxxopt='-ferror-limit=50' \
				--linkopt='-fsanitize=address' \
;

set -o noglob
checks='*'

# I disagree with these checks
checks="${checks},-google-runtime-references"
checks="${checks},-fuchsia-*"

# I need C++11 compatibility
checks="${checks},-modernize-use-trailing-return-type"

# these errors are in <cassert>. I don't know how to ignore that header.
checks="${checks},-hicpp-no-array-decay"
checks="${checks},-cppcoreguidelines-pro-bounds-array-to-pointer-decay"

#checks="${checks},-cert-err58-cpp"

clang-tidy -checks="${checks}" -header-filter='cpu_timer*.*' test/test_cpu_timer.cpp -- -I.
