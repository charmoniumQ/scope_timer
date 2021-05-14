#pragma once // NOLINT(llvm-header-guard)
#if defined(__clang__) || defined(__GNUC__)
#define SCOPE_TIMER_LIKELY(x)      __builtin_expect(!!(x), 1)
#define SCOPE_TIMER_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#define SCOPE_TIMER_UNUSED              [[maybe_unused]]
#else
#define SCOPE_TIMER_LIKELY(x)      x
#define SCOPE_TIMER_UNLIKELY(x)    x
#define SCOPE_TIMER_UNUSED
#endif
