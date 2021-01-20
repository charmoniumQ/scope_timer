#pragma once // NOLINT(llvm-header-guard)
#if defined(__clang__) || defined(__GNUC__)
#define bool_likely(x)      __builtin_expect(!!(x), 1)
#define bool_unlikely(x)    __builtin_expect(!!(x), 0)
#else
#define bool_likely(x)      x
#define bool_unlikely(x)    x
#endif
