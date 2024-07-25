#include <concurrency/hook.h>
#include <concurrency/scheduler.h>
#include <concurrency/coroutine.h>
#include <concurrency/fd_manager.h>
#include <concurrency/timer_manager.h>
#include <base/singleton.hpp>
#include <base/debug.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <cstdarg>

#define HOOKED_FUNCS(op)	\
    op(sleep) 			\
    op(usleep) 			\
    op(nanosleep) 		\
    op(socket) 			\
    op(fcntl)			\
    op(getsockopt) 		\
    op(setsockopt)		\
    op(connect) 		\
    op(accept) 			\
    op(read) 			\
    op(write) 			\
    // op(readv) 			\
    // op(recv) 			\
    // op(recvfrom) 		\
    // op(recvmsg) 		\
    // op(writev) 			\
    // op(send) 			\
    // op(sendto) 			\
    // op(sendmsg) 		\
    // op(close) 			\

using namespace sylar;
namespace cc = sylar::concurrency;

static auto sylar_logger = SYLAR_ROOT_LOGGER();

#define DYNAMIC_LOAD_SYM(func_name)	\
	cc::func_name ## _libc_func = (cc::func_name ## _libc_func_t)::dlsym(RTLD_NEXT, #func_name);

namespace {

static void InitHook() {
	HOOKED_FUNCS(DYNAMIC_LOAD_SYM);
}

struct __InitHookHelper {
	__InitHookHelper() {
		InitHook();
	}
};

static __InitHookHelper s_init_hook_helper {};

} // namespace

namespace sylar {
namespace concurrency {

namespace this_thread {

static thread_local bool tl_enable_hook = false;

void EnableHook(bool on) {
	tl_enable_hook = on;
}

bool IsHooded() {
	return tl_enable_hook;
}

} // namespace this_thread

#define DEFINE_ORIGINAL_LIBC_FUNCS(func_name)	\
	cc::func_name ## _libc_func_t func_name ## _libc_func = nullptr;
HOOKED_FUNCS(DEFINE_ORIGINAL_LIBC_FUNCS)	// do define
#undef DEFINE_ORIGINAL_LIBC_FUNCS

} // namespace concurrency
} // namespace sylar


namespace {

struct TimeoutFlag {
	bool is_timeout = false;
};

} // namespace

template <typename OriginalLibcFunc, typename... Args>
static ssize_t do_io(OriginalLibcFunc libc_func, int fd, unsigned interest_event, Args&&... args) {
	if (!cc::this_thread::IsHooded()) {
		return libc_func(fd, std::forward<Args>(args)...);
	}

	auto& fd_manager = base::Singleton<cc::FdManager>::GetInstance();
	if (not fd_manager.IsExist(fd) || not fd_manager.GetFdContext(fd).is_socket
		|| fd_manager.GetFdContext(fd).user_set_nonblock)
	{
		return libc_func(fd, std::forward<Args>(args)...);
	}

	auto& fd_cxt = fd_manager.GetFdContext(fd);

	std::shared_ptr<TimeoutFlag> tie;	// as guard
	cc::Timer::TimerId timeout_cond_timer_id = cc::TimerManager::kInvalidTimerId;

	while (true) {
		ssize_t num;
		do {
			num = libc_func(fd, std::forward<Args>(args)...);
		} while (num == -1 && errno == EINTR);

		if (num == -1 && errno == EAGAIN) {
			// register interest event to poller And wait it appending
			auto cur_scheduler = cc::this_thread::GetScheduler();

			// set a timer to wait timeout
			auto timeout = fd_cxt.GetTimeout(interest_event);
			if (timeout != cc::FdContext::clock::duration::max()) {
				std::weak_ptr<void> cond(tie);
				timeout_cond_timer_id = cur_scheduler->RunAfterIf(timeout, cond, [cond, cur_scheduler]() {
					auto timeout_flag = cond.lock();
					if (timeout_flag) {
						auto flag = (TimeoutFlag*)timeout_flag.get();
						flag->is_timeout = true;
					}
				});
			}

			cur_scheduler->AppendEvent(fd, interest_event, nullptr);

			cc::Coroutine::YieldCurCoroutineToHold();
			if (not tie->is_timeout) {
				// cancel cond timer
				cur_scheduler->CancelTimer(timeout_cond_timer_id);
				// do io again
			} else {
				SYLAR_ASSERT(not cur_scheduler->HasTimer(timeout_cond_timer_id));
				errno = ETIMEDOUT;
				return -1;
			}
		} else {
			return num;
		}
	}
}

extern "C" unsigned int sleep(unsigned int seconds) {
	if (!cc::this_thread::IsHooded()) {
		return cc::sleep_libc_func(seconds);
	}

	cc::Scheduler* cur_scheduler = cc::this_thread::GetScheduler();
	auto cur_coroutine = cc::this_thread::GetCurrentRunningCoroutine();
	cur_scheduler->RunAfter(std::chrono::seconds(seconds), [cur_scheduler, cur_coroutine]() {
		cur_scheduler->Co(cur_coroutine);
	});
	cc::Coroutine::YieldCurCoroutineToHold();
	return 0;
}

extern "C" int usleep(useconds_t usec) {
	if (!cc::this_thread::IsHooded()) {
		return cc::usleep_libc_func(usec);
	}

	cc::Scheduler* cur_scheduler = cc::this_thread::GetScheduler();
	auto cur_coroutine = cc::this_thread::GetCurrentRunningCoroutine();
	cur_scheduler->RunAfter(std::chrono::microseconds(usec), [cur_scheduler, cur_coroutine]() {
		cur_scheduler->Co(cur_coroutine);
	});
	cc::Coroutine::YieldCurCoroutineToHold();
	return 0;
}

extern "C" int nanosleep(const struct timespec *req, struct timespec *rem) {
	if (!cc::this_thread::IsHooded()) {
		return cc::nanosleep_libc_func(req, rem);
	}

	cc::Scheduler* cur_scheduler = cc::this_thread::GetScheduler();
	auto cur_coroutine = cc::this_thread::GetCurrentRunningCoroutine();
	cur_scheduler->RunAfter(std::chrono::seconds(req->tv_sec) + std::chrono::nanoseconds(req->tv_nsec),
		[cur_scheduler, cur_coroutine]() {
			cur_scheduler->Co(cur_coroutine);
		}
	);
	cc::Coroutine::YieldCurCoroutineToHold();
	return 0;
}

extern "C" int socket(int domain, int type, int protocol) {
	// 若执行此操作的线程没有启用Hook功能，则不记录该socket上下文
	int sock = cc::socket_libc_func(domain, type, protocol);
	if (cc::this_thread::IsHooded() && sock >= 0) {
		// set NONBLOCK flag in FdContext::Constructor if is socket
		base::Singleton<cc::FdManager>::GetInstance().CreateFdContext(sock);
	}

	return sock;
}

extern "C" int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	return cc::connect_libc_func(sockfd, addr, addrlen);
}

extern "C" int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	if (not cc::this_thread::IsHooded()) {
		return cc::accept_libc_func(sockfd, addr, addrlen);
	}

	return do_io(cc::accept_libc_func, sockfd, EPOLLIN, addr, addrlen);
}

extern "C" ssize_t read(int fd, void *buf, size_t count) {
	if (not cc::this_thread::IsHooded()) {
		return cc::read_libc_func(fd, buf, count);
	}

	return do_io(cc::read_libc_func, fd, EPOLLIN, buf, count);
}

extern "C" ssize_t write(int fd, const void *buf, size_t count) {
	if (not cc::this_thread::IsHooded()) {
		return cc::write_libc_func(fd, buf, count);
	}

	return do_io(cc::write_libc_func, fd, EPOLLOUT, buf, count);
}

extern "C" int fcntl(int fd, int cmd, ... /* arg */ ) {
	va_list va;
	va_start(va, cmd);
	auto& fd_manager = base::Singleton<cc::FdManager>::GetInstance();

	switch (cmd) {
	case F_GETFL:
	{
		va_end(va);
		int flags = cc::fcntl_libc_func(fd, cmd);
		if (not fd_manager.IsExist(fd)) {
			return flags;
		}

		auto& fd_ctx = fd_manager.GetFdContext(fd);
		if (not fd_ctx.is_socket || fd_ctx.is_closed) {
			return flags;
		}

		if (fd_ctx.user_set_nonblock) {
			// return flags | O_NONBLOCK;
			SYLAR_ASSERT(flags | O_NONBLOCK);
			return flags;
		} else {
			return flags & ~O_NONBLOCK;
		}
	}
		break;
	case F_SETFL:
	{
		int flags = va_arg(va, int);
		va_end(va);
		if (not fd_manager.IsExist(fd)) {
			return cc::fcntl_libc_func(fd, cmd, flags);
		}
		auto& fd_ctx = fd_manager.GetFdContext(fd);
		if (fd_ctx.is_closed || not fd_ctx.is_socket) {
			return cc::fcntl_libc_func(fd, cmd, flags);
		}

		// set nonblock flag to fd context if user specified
		fd_ctx.user_set_nonblock = flags & O_NONBLOCK;
		if (fd_ctx.sys_set_nonblock) {
			// 补充 O_NONBLOCK，防止被取消
			flags |= O_NONBLOCK;
		}

		// do set the flags
		return cc::fcntl_libc_func(fd, cmd, flags);
	}
		break;
	case F_DUPFD:
	case F_DUPFD_CLOEXEC:
	case F_SETFD:
	case F_SETOWN:
	case F_SETSIG:
	case F_SETLEASE:
	case F_NOTIFY:
	{
		int arg = va_arg(va, int);
		va_end(va);
		return cc::fcntl_libc_func(fd, cmd, arg);
	}
		break;
	case F_GETFD:
	case F_GETOWN:
	case F_GETSIG:
	case F_GETLEASE:
	{
		va_end(va);
		return cc::fcntl_libc_func(fd, cmd);
	}
		break;
	case F_SETLK:
	case F_SETLKW:
	case F_GETLK:
		{
			struct flock* arg = va_arg(va, struct flock*);
			va_end(va);
			return cc::fcntl_libc_func(fd, cmd, arg);
		}
		break;
	case F_GETOWN_EX:
	case F_SETOWN_EX:
		{
			struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
			va_end(va);
			return cc::fcntl_libc_func(fd, cmd, arg);
		}
		break;
	default:
		SYLAR_LOG_WARN(sylar_logger) << "into default case in fcntl-hook, "
				<< "fd=" << fd << ", " << "cmd=" << cmd
				<< ", ignore variable args if exist" << std::endl;
		va_end(va);
		return cc::fcntl_libc_func(fd, cmd);
	}
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    return cc::getsockopt_libc_func(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
	bool is_set_timeout = false;
    if(level == SOL_SOCKET) {
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
			auto& fd_manager = base::Singleton<cc::FdManager>::GetInstance();
			if (fd_manager.IsExist(sockfd)) {
				auto& fd_cxt = fd_manager.GetFdContext(sockfd);
				const struct ::timeval* time_val = static_cast<const ::timeval*>(optval);
				optname == SO_RCVTIMEO
						? fd_cxt.r_timeout = std::chrono::seconds(time_val->tv_sec) + std::chrono::microseconds(time_val->tv_usec)
						: fd_cxt.w_timeout = std::chrono::seconds(time_val->tv_sec) + std::chrono::microseconds(time_val->tv_usec);
				is_set_timeout = true;
			}
        }
    }
    int ret = cc::setsockopt_libc_func(sockfd, level, optname, optval, optlen);

	if (is_set_timeout) {
		SYLAR_ASSERT(ret == 0);
	}

	return ret;
}
