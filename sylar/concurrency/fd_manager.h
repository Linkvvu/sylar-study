#pragma once

#include <base/singleton.hpp>

#include <chrono>
#include <shared_mutex>
#include <unordered_map>

namespace sylar {
namespace concurrency {

struct FdContext {
	using clock = std::chrono::steady_clock;

	explicit FdContext(int fd);

	int fd;
	bool is_socket;
	bool user_set_nonblock;
	clock::duration r_timeout;
	clock::duration w_timeout;
};


class FdManager {
	friend base::Singleton<FdManager>;

public:
	FdContext& CreateFdContext(int fd);

	FdContext& GetFdContext(int fd);

	void RemoveFd(int fd);

private:
	FdManager() = default;

private:
	std::unordered_map<int, FdContext> fdSet_;
	std::shared_mutex rwMutex_;
};

} // namespace concurrency
} // namespace sylar
