#pragma once

#include <base/this_thread.h>

#include <thread>
#include <memory>
#include <future>
#include <functional>

namespace sylar {
namespace concurrency {

/// @brief Noncopyable thread
class Thread final {
public:
	explicit Thread(std::function<void()> func, std::string name = "");

	~Thread() noexcept;

	void Join();

	void Detach();

	pid_t GetTid() const
	{ return tid_; }

	pthread_t GetPthreadId() const
	{ return pthreadId_; }

	const std::string& GetName() const
	{ return name_; }

public:
	static Thread* GetThisThread();

private:
	Thread(const Thread&) = delete;

	Thread& operator=(const Thread&) = delete;

	void ThreadFunc();

private:
	std::promise<void> ready_;
	pid_t tid_ = 0;
	pthread_t pthreadId_ = 0;
	const std::string name_;
	std::function<void()> func_;
	std::unique_ptr<std::thread> thread_ = nullptr;
};

} // namespace concurrency
} // namespace sylar
