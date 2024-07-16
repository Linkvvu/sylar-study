#pragma once
#include <concurrency/scheduler.h>

#include <sys/epoll.h>
#include <unordered_map>
#include <shared_mutex>

namespace sylar {
namespace concurrency {

class Notifier;

struct Event {
	struct {
		std::function<void()> func;
		std::shared_ptr<concurrency::Coroutine> co;
		Scheduler* owner;
	} read_context;

	struct {
		std::function<void()> func;
		std::shared_ptr<concurrency::Coroutine> co;
		Scheduler* owner;
	} write_context;

	int fd;
	unsigned interest_event;
	// unsigned ready_event;
	std::mutex mutex;
};

class EpollPoller {
public:
	EpollPoller(Scheduler* owner);

	~EpollPoller() noexcept;

	/// @brief Poll and handle ready events, wrap events as a coroutine
	void PollAndHandle();

	void AddEvent(int fd, unsigned interest_events, std::function<void()> func);

	Notifier* GetNotifier() const
	{ return notifier_.get(); }

private:
	enum class EventEnum : unsigned {
		kRead,
		kWrite
	};

	void HandleReadyEvent(epoll_event* ready_event_array, size_t length);
	void HandleEpollEvent(Event* event_instance, unsigned ready_event);
	void TriggerAndRemove(Event* event, EventEnum flag);

	/// @brief Update to epoll object
	void Update(int op, Event* event);
	void AssertInSchedulingScope() const
	{ owner_->AssertInSchedulingScope(); }


private:
	concurrency::Scheduler* owner_;
	int epollFd_;
	std::unordered_map<int, std::shared_ptr<Event>> eventSet_;
	std::unique_ptr<Notifier> notifier_;
	mutable std::shared_mutex mutex_;
};

} // namespace concurrency
} // namespace sylar
