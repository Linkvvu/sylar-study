#include <base/debug.h>
#include <concurrency/fd_manager.h>
#include <concurrency/hook.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>

using namespace sylar;
namespace cc = sylar::concurrency;

static auto sylar_logger = SYLAR_ROOT_LOGGER();

cc::FdContext::FdContext(int a_fd)
	: fd(a_fd)
	, is_closed(false)
	, is_socket(false)
	, sys_set_nonblock(false)
	, user_set_nonblock(false)
	, r_timeout(clock::duration::max())
	, w_timeout(clock::duration::max())
{
	struct ::stat stat_buf;
	if (::fstat(fd, &stat_buf) < 0) {
		SYLAR_LOG_ERROR(sylar_logger) << "failed to get the status of fd "
				<< fd << ", think it as non-socket" << std::endl;
	} else {
		is_socket = S_ISSOCK(stat_buf.st_mode);
	}

	if (is_socket) {
		int flags = cc::fcntl_libc_func(fd, F_GETFL);
		if (flags == -1) {
			if (errno == EBADF) {
				throw std::invalid_argument("fd is not an open file descriptor");
			} else {
				throw std::runtime_error("failed to init the fd context, errstr: " + std::string(std::strerror(errno)));
			}
		}

		/// TODO: 处理用户通过socket接口设置NONBLOCK阻塞的情况
		sys_set_nonblock = flags & O_NONBLOCK;
		if (not sys_set_nonblock) {
			int ret = cc::fcntl_libc_func(fd, F_SETFL, O_NONBLOCK);
			SYLAR_ASSERT(ret == 0);
			sys_set_nonblock = true;
		}
	} else {
		// sys_set_nonblock = false;
	}
}


cc::FdContext& cc::FdManager::CreateFdContext(int fd) {
	std::lock_guard<std::shared_mutex> guard(rwMutex_);
	SYLAR_ASSERT(fdSet_.count(fd) == 0);
	auto pair = fdSet_.emplace(fd, cc::FdContext(fd));
	SYLAR_ASSERT(pair.second);
	return pair.first->second;
}

cc::FdContext& cc::FdManager::GetFdContext(int fd) {
	std::shared_lock<std::shared_mutex> shared_guard(rwMutex_);
	SYLAR_ASSERT(fdSet_.count(fd));
	return fdSet_.at(fd);
}

bool cc::FdManager::IsExist(int fd) {
	std::shared_lock<std::shared_mutex> shared_lock(rwMutex_);
	return fdSet_.count(fd) == 1 ? true : false;
}

void cc::FdManager::RemoveFd(int fd) {
	std::lock_guard<std::shared_mutex> guard(rwMutex_);
	SYLAR_ASSERT(fdSet_.count(fd));
	fdSet_.erase(fd);
}

