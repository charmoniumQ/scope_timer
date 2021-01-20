#include "gtest/gtest.h"
#include "include/cpu_timer.hpp"

void foo();

TEST(CpuTimerTest, Works) { // NOLINT(hicpp-special-member-functions,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cert-err58-cpp)
	foo();
}
