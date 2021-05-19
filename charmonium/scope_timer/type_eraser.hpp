#pragma once

#include <memory>

namespace charmonium::scope_timer::detail {
	// In C++17, consider using std::any
	using TypeEraser = std::shared_ptr<void>;

	static const TypeEraser type_eraser_default = TypeEraser{};

	// Helper functions must be injected into scope_timer namespace
	// directly so I will hold them in main instead.

} // namespace charmonium::scope_timer::detail
