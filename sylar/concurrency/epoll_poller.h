#pragma once
#include <concurrency/scheduler.h>

#include <sys/epoll.h>
#include <unordered_map>
#include <shared_mutex>

namespace sylar {
namespace concurrency {

class Notifier;
class TimerManager;

struct Event {
	enum class StateIndex : uint8_t {
		kNew,
		kDeleted,
		kAdded
	};

	struct {
		std::function<void()> func;
		std::shared_ptr<concurrency::Coroutine> co;
	} read_context;

	struct {
		std::function<void()> func;
		std::shared_ptr<concurrency::Coroutine> co;
	} write_context;

	void Reset() {
		this->fd = -1;
		this->interest_event = 0;
		this->state = StateIndex::kNew;
		this->read_context = {};
		this->write_context = {};
	}

	int fd = -1;
	unsigned interest_event = 0;
	StateIndex state = StateIndex::kNew;
	// unsigned ready_event;
	std::mutex mutex;
};

class EpollPoller {
public:
	EpollPoller(Scheduler* owner);

	~EpollPoller() noexcept;

	/// @brief Poll and handle ready events, wrap events as a coroutine
	void PollAndHandle();

	void UpdateEvent(int fd, unsigned interest_events, std::function<void()> func);

	void CancelEvent(int fd, unsigned target_events);

	Scheduler* GetScheduler() const
	{ return owner_; }

	Notifier* GetNotifier() const
	{ return notifier_.get(); }

	TimerManager* GetTimerManager() const
	{ return timerManager_.get(); }

private:
	Event* GetOrCreateEventObj(int fd);
	void CancelEvent(Event* event, unsigned target_events);

private:
	enum class EventEnum : unsigned {
		kRead,
		kWrite
	};

	void HandleReadyEvents(epoll_event* ready_event_array, size_t length);
	void HandleEpollEvents(Event* event_instance, unsigned ready_event);
	void EnqueueAndRemove(Event* event, EventEnum flag);

	/// @brief Update to epoll object
	void Update(int op, Event* event);
	void AssertInSchedulingScope() const
	{ owner_->AssertInSchedulingScope(); }


private:
	concurrency::Scheduler* const owner_;
	int epollFd_;
	std::unordered_map<int, Event*> eventSet_;
	std::unique_ptr<concurrency::Notifier> notifier_;
	std::unique_ptr<concurrency::TimerManager> timerManager_;
	mutable std::shared_mutex mutex_;
};

} // namespace concurrency
} // namespace sylar
