#pragma once

#include <atomic>

namespace sylar {
namespace concurrency {

/// @brief BasicLockable
class SpinMutex {
	SpinMutex() = default;

	~SpinMutex() noexcept = default;

	void lock() {
		while (flag_.test_and_set(std::memory_order::memory_order_acquire))
		{ ;; }
	}

	void unlock() {
		flag_.clear(std::memory_order::memory_order_relaxed);
	}

private:
	std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

} // namespace concurrency
} // namespace sylar
