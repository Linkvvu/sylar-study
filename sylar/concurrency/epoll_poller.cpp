#include <base/log.h>
#include <base/debug.h>
#include <concurrency/epoll_poller.h>

#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

using namespace sylar;
namespace cc = sylar::concurrency;

static auto sys_logger = SYLAR_SYS_LOGGER();

cc::EpollPoller::EpollPoller(Scheduler* owner)
	: owner_(owner)
	, epollFd_(::epoll_create1(O_CLOEXEC))
{
	if (epollFd_ == -1) {
		SYLAR_LOG_FMT_FATAL(sys_logger, "failed to invoke ::epoll_create, errno=%d, errstr: %s, about to exit\n"
				, errno, std::strerror(errno));
		std::abort();
	}
}

void cc::EpollPoller::AddEvent(int fd, unsigned interest_events, std::function<void()> func) {
	// FIXME: 应该支持追加事件
	Event* event;
	{
		std::lock_guard<std::shared_mutex> guard(mutex_);
		SYLAR_ASSERT(eventSet_.count(fd) == 0 || eventSet_[fd] == nullptr);
		eventSet_[fd] = std::make_shared<Event>();
		event = eventSet_[fd].get();
	}

	std::lock_guard<std::mutex> guard(event->mutex);
	event->fd = fd;
	event->interest_event = EPOLLET | interest_events;
	Update(EPOLL_CTL_ADD, event);

	if (event->interest_event & EPOLLIN) {
		event->read_context.owner = this->owner_;
		if (func) {
			event->read_context.func = std::move(func);
		} else {
			event->read_context.co = cc::this_thread::GetCurrentRunningCoroutine();		/// ?????
		}
	}

	if (event->interest_event & EPOLLOUT) {
		event->write_context.owner = this->owner_;
		if (func) {
			event->write_context.func = std::move(func);
		} else {
			event->write_context.co = cc::this_thread::GetCurrentRunningCoroutine();	/// ?????
		}
	}
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
			HandleReadyEvent(unique_p_e_e.get(), static_cast<size_t>(num));
			break;
		}
	}

}

void cc::EpollPoller::HandleReadyEvent(epoll_event* ready_event_array, size_t length) {
	for (size_t i = 0; i < length; ++i) {
		auto cur_e_e = ready_event_array[i];
		Event* current_event = static_cast<Event*>(cur_e_e.data.ptr);

#ifndef NDEBUG
		{
			std::shared_lock<std::shared_mutex> read_lock(this->mutex_);
			SYLAR_ASSERT(eventSet_.count(current_event->fd) == 1);
			SYLAR_ASSERT(eventSet_[current_event->fd].get() == current_event);
		}
#endif

		std::lock_guard<std::mutex> guard(current_event->mutex);
		if ((current_event->interest_event & cur_e_e.events) == 0) {
			// 当前就绪的事件已被其他线程接收，考虑用EPOLLONESHOT同步
			continue;
		}

		// 更新兴趣事件，只关注剩余的事件, 设置 EPOLLET 为避免多线程惊群现象, 用于同步多线程同时在一个epoll实例上等待
		unsigned left_events = EPOLLET | (current_event->interest_event & ~cur_e_e.events);
		int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
		Update(op, current_event);
		HandleEpollEvent(current_event, cur_e_e.events);
	}

}

void cc::EpollPoller::Update(int op, Event* event) {
	struct ::epoll_event e_e;
	std::memset(&e_e, 0, sizeof e_e);
	e_e.data.ptr = event;
	e_e.events = event->interest_event;

	if (::epoll_ctl(epollFd_, op, event->fd, &e_e) < 0) {
      SYLAR_LOG_ERROR(sys_logger) << "failed to invoke ::epoll_ctl, op="
	  		<< op << ", fd=" << event->fd << std::endl;
	}
}

void cc::EpollPoller::HandleEpollEvent(Event* event_instance, unsigned ready_event) {
	if ((ready_event & EPOLLHUP) && !(ready_event & EPOLLIN)) {
		SYLAR_LOG_WARN(sys_logger) << "fd " << event_instance->fd
				<< " is hung up, about to close it" << std::endl;
		/// TODO: close current fd
	}

	if (ready_event & EPOLLERR) {
		/// FIXME: use ::getsockopt to get error
		SYLAR_LOG_WARN(sys_logger) << "fd " << event_instance->fd
				<< " occurred a error, errno=" << errno
				<< ", errstr: " << std::strerror(errno)
				 << std::endl;
	}

	if (ready_event & (EPOLLIN | EPOLLRDHUP | EPOLLPRI)) {
		// 封装读任务协程入队
		TriggerAndRemove(event_instance, EventEnum::kRead);
	}

	if (ready_event & EPOLLOUT) {
		// 封装写任务协程入队
		TriggerAndRemove(event_instance, EventEnum::kRead);
	}
}

void cc::EpollPoller::TriggerAndRemove(Event* event, EventEnum flag) {
	switch (flag) {
	case EventEnum::kRead:
		SYLAR_ASSERT(event->read_context.func || event->read_context.co);
		SYLAR_ASSERT(event->read_context.owner);

		if (event->read_context.co) {
			event->read_context.owner->Co(std::move(event->read_context.co));
		} else {
			event->read_context.owner->Co(std::move(event->read_context.func));
		}
		break;
	case EventEnum::kWrite:
		SYLAR_ASSERT(event->write_context.func || event->write_context.co);
		SYLAR_ASSERT(event->write_context.owner);
		if (event->write_context.co) {
			event->write_context.owner->Co(std::move(event->write_context.co));
		} else {
			event->write_context.owner->Co(std::move(event->write_context.func));
		}
		break;
	}
}
