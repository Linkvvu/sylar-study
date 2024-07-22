#include <concurrency/timer_manager.h>
#include <concurrency/epoll_poller.h>
#include <base/log.h>
#include <base/debug.h>

#include <sys/timerfd.h>
#include <unistd.h>
#include <cstring>

using namespace sylar;
namespace cc = sylar::concurrency;

static auto sys_logger = SYLAR_SYS_LOGGER();

namespace {

static int CreateTimerFd() {
    int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd == -1) {
    	SYLAR_LOG_FATAL(sys_logger) << "failed to create timerfd, about to exit" << std::endl;
		std::abort();
    }
    return fd;
}

static std::atomic<cc::Timer::TimerId> gs_next_timer_id(0);

} // namespace

cc::TimerManager::TimerManager(EpollPoller* owner)
	: owner_(owner)
	, timerFd_(::CreateTimerFd())
	, latestTime_(decltype(latestTime_)::max())
	{}

cc::TimerManager::~TimerManager() noexcept {
	::close(timerFd_);
}

void cc::TimerManager::AddTimer(Timer timer) {
	SYLAR_ASSERT(timer.timeout_tp != decltype(timer.timeout_tp)::max());

	std::lock_guard<std::mutex> guard(mutex_);
	auto cur_timeout_tp = timer.timeout_tp;
	bool latest_need_update = AddToHeap(std::move(timer));
	if (latest_need_update) {
		latestTime_ = cur_timeout_tp;
		RefreshTimerFd();
	}
}

void cc::TimerManager::CancelTimer(Timer::TimerId target) {
	std::lock_guard<std::mutex> guard(mutex_);
	auto it = std::find_if(timerList_.begin(), timerList_.end(), [target](const Timer& t) {
		return t.id == target;
	});

	SYLAR_ASSERT(it != timerList_.end());
	RemoveFromHeap(it);
}



void cc::TimerManager::HandleExpiredTimers() {
    uint64_t count = 0;
    int ret = ::read(timerFd_, &count, sizeof count);
    if (ret != sizeof count) {
        SYLAR_LOG_ERROR(sys_logger) << "failed to invoke ::read on timerfd"
				<< ", errno=" << errno << ", errstr: " << std::strerror(errno)
				<< std::endl;
    }

    auto expired_timers = GetAllExpiredTimers();
    for (auto& t : expired_timers) {
		owner_->GetScheduler()->Co(std::move(t.cb));
    }
}

cc::Timer::TimerId cc::TimerManager::GetNextTimerId() {
	return gs_next_timer_id.fetch_add(1, std::memory_order::memory_order_relaxed);
}

bool cc::TimerManager::AddToHeap(Timer&& timer) {
	bool latest_need_update = false;
	if (timerList_.empty() || timerList_.begin()->timeout_tp > timer.timeout_tp) {
		latest_need_update = true;
	}

	timerList_.insert(std::move(timer));

	return latest_need_update;
}

void cc::TimerManager::RemoveFromHeap(std::set<cc::Timer>::iterator it) {
	bool latest_need_update = false;
	if (timerList_.size() == 1 || it == timerList_.begin()) {
		latest_need_update = true;
	}

	timerList_.erase(it);

	if (latest_need_update) {
		if (timerList_.empty()) {
			latestTime_ = decltype(latestTime_)::max();
		} else {
			latestTime_ = timerList_.begin()->timeout_tp;
		}

		RefreshTimerFd();
	}

}

void cc::TimerManager::RefreshTimerFd() {
    using namespace std;

    struct ::itimerspec old_t, new_t;
	std::memset(&new_t, 0, sizeof new_t);

    if (latestTime_ != cc::Timer::TimePoint::max()) {
        auto duration = latestTime_.time_since_epoch();
        auto sec = chrono::duration_cast<chrono::seconds>(duration);
        auto nsec =  chrono::duration_cast<chrono::nanoseconds>(duration - sec);
        new_t.it_value.tv_sec = static_cast<decltype(itimerspec::it_value.tv_sec)>(sec.count());
        new_t.it_value.tv_nsec = static_cast<decltype(itimerspec::it_value.tv_nsec)>(nsec.count());
    }

	int ret = ::timerfd_settime(timerFd_, TFD_TIMER_ABSTIME, &new_t, &old_t);
	if (ret < 0) {
		SYLAR_LOG_ERROR(sys_logger)
				<< "failed to set timerfd, errno=" << errno
				<< " errstr: " << std::strerror(errno) << std::endl;
	}
}

std::vector<cc::Timer> cc::TimerManager::GetAllExpiredTimers() {
	std::vector<Timer> expired_timers;

	std::lock_guard<std::mutex> guard(mutex_);

	while (!timerList_.empty() && timerList_.begin()->timeout_tp <= std::chrono::steady_clock::now()) {
		auto it = timerList_.begin();
		expired_timers.push_back(*it);
		if (it->IsRepeated()) {
			auto next_expiration = it->interval + std::chrono::steady_clock::now();

			Timer next_timer = *it;
			next_timer.SetExpiration(next_expiration);

			timerList_.erase(it);
			timerList_.insert(std::move(next_timer));
		} else {
			timerList_.erase(it);
		}
	}

	if (timerList_.empty()) {
		latestTime_ = decltype(latestTime_)::max();
	} else {
		SYLAR_ASSERT(timerList_.begin()->timeout_tp > latestTime_);
		latestTime_ = timerList_.begin()->timeout_tp;
	}

	RefreshTimerFd();

    return expired_timers;
}
