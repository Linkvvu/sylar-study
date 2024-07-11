#pragma once

#include <concurrency/coroutine.h>

#include <mutex>
#include <atomic>
#include <vector>
#include <memory>
#include <functional>

namespace sylar {
namespace concurrency {

class Thread;

class Scheduler {
public:
    explicit Scheduler(size_t thread_num, bool include_cur_thread, std::string name);

	virtual ~Scheduler() noexcept;

	void Start();

private:
	void ScheduleFunc();

	struct Invokable {
		explicit Invokable(const std::shared_ptr<concurrency::Coroutine>& co, ::pthread_t pthread_id = 0);
		explicit Invokable(std::shared_ptr<concurrency::Coroutine>&& co, ::pthread_t pthread_id = 0);

		explicit Invokable(const std::function<void()>& cb, ::pthread_t pthread_id = 0);
		explicit Invokable(std::function<void()>&& cb, ::pthread_t pthread_id = 0);

		void Reset();

		::pthread_t target_thread;
		std::function<void()> callback;
		std::shared_ptr<concurrency::Coroutine> coroutine;
	};

private:
    std::string name_;
    std::unique_ptr<concurrency::Coroutine> rootCoroutine_;
	::pthread_t rootPthreadId_;
    std::vector<std::unique_ptr<concurrency::Thread>> threadPool_;
	std::atomic<bool> stopped_;
	std::vector<Scheduler::Invokable> taskList_ {};
	mutable std::mutex mutex_;
};

} // namespace concurrency
} // namespace sylar
