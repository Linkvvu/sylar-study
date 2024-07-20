#pragma once

#include <sys/eventfd.h>

namespace sylar {
namespace concurrency {

class EpollPoller;

class Notifier {
public:
	Notifier(EpollPoller* owner);

	~Notifier() noexcept;

	/// @brief 信号量累加 @a num
	void Notify(uint64_t num = 1);

	int GetEventFd()
	{ return eventFd_; }

	void HandleEventFd();

private:
	EpollPoller* owner_;
	int eventFd_;
};

} // namespace concurrency
} // namespace sylar
