#include <concurrency/hook.h>
#include <concurrency/scheduler.h>
#include <concurrency/coroutine.h>
#include <concurrency/fd_manager.h>
#include <base/singleton.hpp>

#include <dlfcn.h>
#include <fcntl.h>
#include <cstdarg>

#define HOOKED_FUNCS(op)	\
    op(sleep) 			\
    op(usleep) 			\
    op(nanosleep) 		\
    op(socket) 			\
    op(fcntl)			\
    // op(connect) 		\
    // op(accept) 			\
    // op(read) 			\
    // op(readv) 			\
    // op(recv) 			\
    // op(recvfrom) 		\
    // op(recvmsg) 		\
    // op(write) 			\
    // op(writev) 			\
    // op(send) 			\
    // op(sendto) 			\
    // op(sendmsg) 		\
    // op(close) 			\
    // op(ioctl) 			\
    // op(getsockopt) 		\
    // op(setsockopt)

using namespace sylar;
namespace cc = sylar::concurrency;

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



extern "C" unsigned int sleep(unsigned int seconds) {
	if (!cc::this_thread::IsHooded()) {
		return cc::sleep_libc_func(seconds);
	}

	cc::Scheduler* cur_scheduler = cc::this_thread::GetScheduler();
	cur_scheduler->RunAfter(std::chrono::seconds(seconds), nullptr);
	cc::Coroutine::YieldCurCoroutineToHold();
	return 0;
}

extern "C" int usleep(useconds_t usec) {
	if (!cc::this_thread::IsHooded()) {
		return cc::usleep_libc_func(usec);
	}

	cc::Scheduler* cur_scheduler = cc::this_thread::GetScheduler();
	cur_scheduler->RunAfter(std::chrono::microseconds(usec), nullptr);
	cc::Coroutine::YieldCurCoroutineToHold();
	return 0;
}

extern "C" int nanosleep(const struct timespec *req, struct timespec *rem) {
	if (!cc::this_thread::IsHooded()) {
		return cc::nanosleep_libc_func(req, rem);
	}

	cc::Scheduler* cur_scheduler = cc::this_thread::GetScheduler();
	cur_scheduler->RunAfter(std::chrono::seconds(req->tv_sec) + std::chrono::nanoseconds(req->tv_nsec), nullptr);
	cc::Coroutine::YieldCurCoroutineToHold();
	return 0;
}

extern "C" int socket(int domain, int type, int protocol) {
	// 若执行此操作的线程没有启用Hook功能，则不记录该socket上下文
	int sock = cc::socket_libc_func(domain, type, protocol);
	if (cc::this_thread::IsHooded() && sock >= 0) {
		/// @note Doesn't set O_NONBLOCK, 而是等待poller通知事件就绪后再执行IO
		/// cc::fcntl_libc_func(sock, F_SETFL, O_NONBLOCK);

		base::Singleton<cc::FdManager>::GetInstance().CreateFdContext(sock);
	}

	return sock;
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
			return flags | O_NONBLOCK;
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
	}

}
