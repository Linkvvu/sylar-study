#pragma once

#include <sys/eventfd.h>

namespace sylar {
namespace concurrency {

class Scheduler;

class Notifier {
public:
	Notifier(Scheduler* owner);

	~Notifier() noexcept;

	/// @brief 信号量累加1
	void Notify(uint64_t num = 1);

	int GetEventFd()
	{ return eventFd_; }

	void HandleEventFd();

private:
	Scheduler* owner_;
	int eventFd_;
};

} // namespace concurrency
} // namespace sylar
