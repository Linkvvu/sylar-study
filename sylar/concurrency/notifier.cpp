#include <concurrency/notifier.h>
#include <concurrency/scheduler.h>
#include <base/log.h>

#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>

using namespace sylar;
namespace cc = sylar::concurrency;

static auto sys_logger = SYLAR_SYS_LOGGER();

cc::Notifier::Notifier(EpollPoller* owner)
	: owner_(owner)
	, eventFd_(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE))
{
	if (eventFd_ < 0) {
		SYLAR_LOG_FATAL(sys_logger) << "failed to create eventfd object, about to exit" << std::endl;
		std::abort();
	}
}


cc::Notifier::~Notifier() {
	::close(eventFd_);
}

void cc::Notifier::Notify(uint64_t num) {
	int ret = ::write(eventFd_, &num, sizeof num);
	if (__builtin_expect(ret < 0, 0)) {
		SYLAR_LOG_ERROR(sys_logger) << "failed to invoke ::write on eventfd object"
			<< ", num=" << num <<", errno=" << errno
			<< ", errstr:" << std::strerror(errno) << std::endl;
	}
}

void cc::Notifier::HandleEventFd() {
	char temp_buffer[64] {};
	int ret = ::read(eventFd_, temp_buffer, 64);
	if (__builtin_expect(ret < 0, 0)) {
		SYLAR_LOG_WARN(sys_logger) << "failed to invoke ::read on eventfd object"
			<<", errno=" << errno << ", errstr: " << std::strerror(errno) << std::endl;
	}
}
