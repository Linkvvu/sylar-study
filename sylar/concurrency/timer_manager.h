#pragma once

#include <set>
#include <mutex>
#include <chrono>
#include <memory>
#include <functional>

namespace sylar {
namespace concurrency {

class EpollPoller;

struct Timer {
	using TimerId = uint32_t;
	using TimePoint = std::chrono::steady_clock::time_point;
	using Interval = std::chrono::steady_clock::duration;

	explicit Timer(TimerId a_id, TimePoint a_timeout_tp, Interval a_interval, std::function<void()> a_cb)
		: id(a_id)
		, timeout_tp(std::move(a_timeout_tp))
		, interval(std::move(a_interval))
		, cb(std::move(a_cb))
		{}

	bool operator<(const Timer& other) const {
		return this->timeout_tp < other.timeout_tp;
	}

	bool IsRepeated() const
	{ return interval != Interval::zero(); }

	void SetExpiration(TimePoint tp)
	{ timeout_tp = tp; }

	TimerId id;
	TimePoint timeout_tp;
	Interval interval;
	std::function<void()> cb;
};

class TimerManager {
public:
	explicit TimerManager(EpollPoller* owner);

	~TimerManager() noexcept;

	int GetTimerFd() const
	{ return timerFd_; }

	void AddTimer(Timer);

	void AddConditionTimer(Timer, std::weak_ptr<void> cond);

	void CancelTimer(Timer::TimerId);

	bool HasTimer(Timer::TimerId);

	void HandleExpiredTimers();

	static Timer::TimerId GetNextTimerId();

public:
	constexpr static const Timer::TimerId kInvalidTimerId = 0;

private:
	bool AddToHeap(Timer&& timer);
	void RemoveFromHeap(std::set<concurrency::Timer>::iterator);
	void RefreshTimerFd();
	std::vector<Timer> GetAllExpiredTimers();

public:
	EpollPoller* owner_;
	int timerFd_;
	std::set<Timer> timerList_;
	Timer::TimePoint latestTime_;
	mutable std::mutex mutex_;
};

} // namespace concurrency
} // namespace sylar
