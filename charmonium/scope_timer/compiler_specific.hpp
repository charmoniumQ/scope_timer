#pragma once // NOLINT(llvm-header-guard)
#if defined(__clang__) || defined(__GNUC__)
#define CHARMONIUM_SCOPE_TIMER_LIKELY(x)      __builtin_expect(!!(x), 1)
#define CHARMONIUM_SCOPE_TIMER_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#define CHARMONIUM_SCOPE_TIMER_UNUSED         [[maybe_unused]]
#else
#define CHARMONIUM_SCOPE_TIMER_LIKELY(x)      x
#define CHARMONIUM_SCOPE_TIMER_UNLIKELY(x)    x
#define CHARMONIUM_SCOPE_TIMER_UNUSED
#endif
