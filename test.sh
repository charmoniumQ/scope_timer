#!/bin/bash
bazel test //test:cpu_timer_test \
				--cxxopt='-std=c++11' \
				--cxxopt='-Wall' \
				--cxxopt='-Wextra' \
				--cxxopt='-Werror' \
				--cxxopt='-fsanitize=address' \
				--cxxopt='-g' \
				--linkopt='-fsanitize=address'

set -o noglob
checks='*'
checks="${checks},-modernize-use-trailing-return-type"
checks="${checks},-fuchsia-*"
checks="${checks},-llvm-header-guard"
checks="${checks},-google-runtime-references"
checks="${checks},-hicpp-special-member-functions"
checks="${checks},-cppcoreguidelines-special-member-functions"
checks="${checks},-hicpp-no-array-decay"
checks="${checks},-cppcoreguidelines-pro-bounds-array-to-pointer-decay"
checks="${checks},-cppcoreguidelines-owning-memory"
checks="${checks},-cert-err58-cpp"

clang-tidy -checks="${checks}" -header-filter='cpu_timer*.*' test/test_cpu_timer.cpp -- -I.
