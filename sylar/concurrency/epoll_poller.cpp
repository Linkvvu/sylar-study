#include <base/log.h>
#include <base/debug.h>
#include <concurrency/notifier.h>
#include <concurrency/epoll_poller.h>
#include <concurrency/timer_manager.h>

#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

using namespace sylar;
namespace cc = sylar::concurrency;

static auto sys_logger = SYLAR_SYS_LOGGER();
static auto sylar_logger = SYLAR_ROOT_LOGGER();

cc::EpollPoller::EpollPoller(Scheduler* owner)
	: owner_(owner)
	, epollFd_(::epoll_create1(O_CLOEXEC))
	, notifier_(std::make_unique<Notifier>(this))
	, timerManager_(std::make_unique<TimerManager>(this))
{
	if (epollFd_ == -1) {
		SYLAR_LOG_FMT_FATAL(sys_logger, "failed to invoke ::epoll_create, errno=%d, errstr: %s, about to exit\n"
				, errno, std::strerror(errno));
		std::abort();
	}

	/// FIXME:
	///		重构Notifier/TimerManager与Poller的关系
	struct ::epoll_event notifier_e_e;
	std::memset(&notifier_e_e, 0, sizeof(::epoll_event));
	notifier_e_e.data.fd = notifier_->GetEventFd();
	notifier_e_e.events = EPOLLIN;
	::epoll_ctl(epollFd_, EPOLL_CTL_ADD, notifier_->GetEventFd(), &notifier_e_e);

	struct ::epoll_event timerfd_e_e;
	std::memset(&timerfd_e_e, 0, sizeof(::epoll_event));
	timerfd_e_e.data.fd = timerManager_->GetTimerFd();
	timerfd_e_e.events = EPOLLIN | EPOLLET;
	::epoll_ctl(epollFd_, EPOLL_CTL_ADD, timerManager_->GetTimerFd(), &timerfd_e_e);
}

cc::EpollPoller::~EpollPoller() noexcept {
	::epoll_ctl(epollFd_, EPOLL_CTL_DEL, notifier_->GetEventFd(), nullptr);
	::epoll_ctl(epollFd_, EPOLL_CTL_DEL, timerManager_->GetTimerFd(), nullptr);
	::close(epollFd_);

	for (const auto& pair : eventSet_) {
		delete pair.second;
	}
}

void cc::EpollPoller::UpdateEvent(int fd, unsigned interest_events, std::function<void()> func) {
	SYLAR_ASSERT(interest_events != 0);

	// get event object
	Event* event = GetOrCreateEventObj(fd);

	// manipulate event object
	std::lock_guard<std::mutex> event_guard(event->mutex);
	// 设置 EPOLLET 为避免多线程惊群现象
	event->interest_event = EPOLLET | interest_events;
	int op = event->state == Event::StateIndex::kAdded
		? EPOLL_CTL_MOD
		: EPOLL_CTL_ADD;
	Update(op, event);
	event->state = Event::StateIndex::kAdded;

	// add callback
	if (interest_events & EPOLLIN) {
		if (func) {
			event->read_context.func = std::move(func);
		} else {
			event->read_context.co = cc::this_thread::GetCurrentRunningCoroutine();		/// ?????
		}
	}

	if (interest_events & EPOLLOUT) {
		if (func) {
			event->write_context.func = std::move(func);
		} else {
			event->write_context.co = cc::this_thread::GetCurrentRunningCoroutine();	/// ?????
		}
	}
}

void cc::EpollPoller::CancelEvent(int fd, unsigned target_events) {
	// get event object
	Event* event = GetOrCreateEventObj(fd);

	std::lock_guard<std::mutex> event_guard(event->mutex);
	bool has_target_events = event->interest_event & target_events;
	if (!has_target_events) {
		SYLAR_LOG_WARN(sylar_logger) << "failed to cancel event, has no events" << target_events << " on fd " << fd;
		return;
	}

	CancelEvent(event, target_events);
}

void sylar::concurrency::EpollPoller::CancelEvent(Event* event, unsigned target_events) {
	SYLAR_ASSERT(event->state == Event::StateIndex::kAdded);

	event->interest_event &= ~target_events;
	int op = event->interest_event & ~EPOLLET
		? EPOLL_CTL_MOD
		: EPOLL_CTL_DEL;
	Update(op, event);

	if (op == EPOLL_CTL_DEL) {
		event->state = Event::StateIndex::kDeleted;
	}

	if (target_events & EPOLLIN) {
		event->read_context.func = nullptr;
		event->read_context.co = nullptr;
	}

	if (target_events & EPOLLOUT) {
		event->read_context.func = nullptr;
		event->read_context.co = nullptr;
	}
}

cc::Event* cc::EpollPoller::GetOrCreateEventObj(int fd) {
	{	// try to get
		std::shared_lock<std::shared_mutex> shared_guard(mutex_);
		bool exist = eventSet_.count(fd);
		if (exist)
			return eventSet_[fd];
	}

	{
		std::lock_guard<std::shared_mutex> guard(mutex_);
		if (eventSet_[fd] == nullptr) {
			auto new_event = new Event();
			new_event->fd = fd;
			eventSet_[fd] = new_event;
		}
	}
	return eventSet_[fd];
}

void cc::EpollPoller::PollAndHandle() {
#define EPOLL_TIMEOUT 5000
#define EPOLL_MAX_EVENT 64
	AssertInSchedulingScope();

	// a coroutine usually has a tiny stack size, allocate on head
	std::unique_ptr<epoll_event, void(*)(epoll_event*)> unique_p_e_e
		(new epoll_event[EPOLL_MAX_EVENT],
		[](epoll_event* event_array) {
			delete[] event_array;
		});

	while (true) {
		int num = ::epoll_wait(epollFd_, unique_p_e_e.get(),EPOLL_MAX_EVENT, EPOLL_TIMEOUT);
		if (num < 0) {
			if (errno == EINTR) { continue; }
			else {
				SYLAR_LOG_ERROR(sys_logger) << "occur a error when invoke ::epoll_wait"
						<< ", errno=" << errno << ", errstr: " << std::strerror(errno)
						<< ", continue polling" << std::endl;
			}
			continue;
		} else if (num == 0) {
			// timeout and no ready event
			continue;
		} else {
			HandleReadyEvents(unique_p_e_e.get(), static_cast<size_t>(num));
			break;
		}
	}

}

void cc::EpollPoller::HandleReadyEvents(epoll_event* ready_event_array, size_t length) {
	for (size_t i = 0; i < length; ++i) {
		auto cur_e_e = ready_event_array[i];

		if (cur_e_e.data.fd == notifier_->GetEventFd()) {
			notifier_->HandleEventFd();
			continue;
		} else if (cur_e_e.data.fd == timerManager_->GetTimerFd()) {
			timerManager_->HandleExpiredTimers();
			continue;
		}

		Event* current_event = static_cast<Event*>(cur_e_e.data.ptr);

#ifndef NDEBUG
		{
			std::shared_lock<std::shared_mutex> read_lock(this->mutex_);
			SYLAR_ASSERT(eventSet_.count(current_event->fd) == 1);
			SYLAR_ASSERT(eventSet_[current_event->fd] == current_event);
		}
#endif

		std::lock_guard<std::mutex> guard(current_event->mutex);
		if ((current_event->interest_event & cur_e_e.events) == 0) {
			// 当前就绪的事件已被其他线程接收，考虑用EPOLLONESHOT同步
			continue;
		}

		// 处理就绪的EPOLL事件
		HandleEpollEvents(current_event, cur_e_e.events);
		// 更新兴趣事件，只关注剩余的事件
		CancelEvent(current_event, cur_e_e.events);
	}
}

void cc::EpollPoller::Update(int op, Event* event) {
	struct ::epoll_event e_e;
	std::memset(&e_e, 0, sizeof e_e);
	e_e.data.ptr = event;
	e_e.events = event->interest_event;

	if (::epoll_ctl(epollFd_, op, event->fd, &e_e) < 0) {
      SYLAR_LOG_ERROR(sys_logger) << "failed to invoke ::epoll_ctl, op="
	  		<< op << ", fd=" << event->fd << ", errno=" << errno
			<< " errstr: " << std::strerror(errno) << std::endl;
	}
}

void cc::EpollPoller::HandleEpollEvents(Event* event_instance, unsigned ready_event) {
	if ((ready_event & EPOLLHUP) && !(ready_event & EPOLLIN)) {
		SYLAR_LOG_WARN(sys_logger) << "fd " << event_instance->fd
				<< " is hung up, about to close it" << std::endl;
		/// TODO: close current fd
	}

	if (ready_event & EPOLLERR) {
		/// TODO: use ::getsockopt to get error
		ready_event |= EPOLLIN | EPOLLOUT;
	}

	if (ready_event & (EPOLLIN | EPOLLRDHUP | EPOLLPRI)) {
		// 封装读任务协程入队
		EnqueueAndRemove(event_instance, EventEnum::kRead);
	}

	if (ready_event & EPOLLOUT) {
		// 封装写任务协程入队
		EnqueueAndRemove(event_instance, EventEnum::kWrite);
	}
}

void cc::EpollPoller::EnqueueAndRemove(Event* event, EventEnum flag) {
	switch (flag) {
	case EventEnum::kRead:
		SYLAR_ASSERT(event->read_context.func || event->read_context.co);

		if (event->read_context.co) {
			this->owner_->Co(std::move(event->read_context.co));
		} else {
			this->owner_->Co(std::move(event->read_context.func));
		}
		break;
	case EventEnum::kWrite:
		SYLAR_ASSERT(event->write_context.func || event->write_context.co);

		if (event->write_context.co) {
			this->owner_->Co(std::move(event->write_context.co));
		} else {
			this->owner_->Co(std::move(event->write_context.func));
		}
		break;
	}
}
