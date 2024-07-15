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

namespace this_thread {

Scheduler* GetScheduler();
Coroutine* GetSchedulingCoroutine();

} // namespace this_thread

class Scheduler {
public:
    explicit Scheduler(size_t thread_num, bool include_cur_thread, std::string name);

	virtual ~Scheduler() noexcept;

	void Start();

	void Stop();

	bool IsStopped() const;

	/// TODO:
	/// 	检测Invocable内部是否含有可调用对象
	template <typename Invocable>
	void Co(Invocable&& func, pthread_t target_thread = 0);

	/// @brief 断言当前是否在该调度器所管理的线程中执行
	void AssertInScheduleingScope() const;

private:
	void SchedulingFunc();

	template <typename Invocable>
	void AddTaskNoLock(Invocable&& func, pthread_t target_thread);

	virtual void HandleIdle();

	struct InvocableWrapper {
		InvocableWrapper() : target_thread(0), callback(nullptr), coroutine(nullptr) {}
		explicit InvocableWrapper(const std::shared_ptr<concurrency::Coroutine>& co, ::pthread_t pthread_id = 0);
		explicit InvocableWrapper(std::shared_ptr<concurrency::Coroutine>&& co, ::pthread_t pthread_id = 0);

		explicit InvocableWrapper(const std::function<void()>& cb, ::pthread_t pthread_id = 0);
		explicit InvocableWrapper(std::function<void()>&& cb, ::pthread_t pthread_id = 0);

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
	std::atomic<bool> stopped_ {true};
	std::atomic<size_t> activeThreadNum_ {0};
	std::atomic<size_t> idleThreadNum_ {0};
	std::vector<Scheduler::InvocableWrapper> taskList_;
	mutable std::mutex mutex_;
};

template<typename Invocable>
void Scheduler::Co(Invocable&& func, pthread_t target_thread) {
	std::lock_guard<std::mutex> guard(mutex_);
	AddTaskNoLock(std::forward<Invocable>(func), target_thread);
}

template<typename Invocable>
void Scheduler::AddTaskNoLock(Invocable&& func, pthread_t target_thread) {
	taskList_.emplace_back(std::forward<Invocable>(func), target_thread);
}

} // namespace concurrency
} // namespace sylar
