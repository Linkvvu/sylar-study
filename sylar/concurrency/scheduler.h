#pragma once

#include <concurrency/coroutine.h>

#include <list>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>

namespace sylar {
namespace concurrency {

class Thread;
class EpollPoller;

namespace this_thread {

Scheduler* GetScheduler();
Coroutine* GetSchedulingCoroutine();

} // namespace this_thread

class Scheduler {
public:
    explicit Scheduler(size_t thread_num, bool include_cur_thread, std::string name);

	~Scheduler() noexcept;

	void Start();

	void Stop();

	bool IsStopped() const;

	/// TODO:
	/// 	检测Invocable内部是否含有可调用对象
	/// @param target_thread  为当前任务指定一个特定线程执行，若为0则不指定
	template <typename Invocable>
	void Co(Invocable&& func, pthread_t target_thread = 0);

	/// @brief 断言当前是否在该调度器所管理的线程中执行
	void AssertInSchedulingScope() const;

	void AppendEvent(int fd, unsigned interest_events, std::function<void()> func);

	void UpdateEvent(int fd, unsigned interest_events, std::function<void()> func);

	void CancelEvent(int fd, unsigned target_events);

	uint32_t RunAt(std::chrono::steady_clock::time_point tp, std::function<void()> cb);
	uint32_t RunAtIf(std::chrono::steady_clock::time_point tp, std::weak_ptr<void> cond, std::function<void()> cb);
	bool HasTimer(uint32_t timer_id);
	uint32_t RunAfter(std::chrono::steady_clock::duration dur, std::function<void()> cb, bool repeated = false);
	uint32_t RunAfterIf(std::chrono::steady_clock::duration dur, std::weak_ptr<void> cond, std::function<void()> cb, bool repeated = false);

	void CancelTimer(uint32_t timer_id);

private:
	void SchedulingFunc();

	/// @brief 不加锁的向任务队列中添加任务
	/// @return 若原队列为空，则返回 true ，用于判断是否需要通知其他线程有任务就绪
	template <typename Invocable>
	bool AddTaskNoLock(Invocable&& func, pthread_t target_thread);

	void HandleIdle();

	/// @brief 添加 num 个信号量
	void Notify(uint64_t num = 1);

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
    std::shared_ptr<concurrency::Coroutine> dummyMainCoroutine_;
	::pthread_t dummyMainTrdPthreadId_;
	std::unique_ptr<concurrency::EpollPoller> poller_;
    std::vector<std::unique_ptr<concurrency::Thread>> threadPool_;
	std::atomic<bool> stopped_ {true};
	std::atomic<size_t> activeThreadNum_ {0};
	std::atomic<size_t> idleThreadNum_ {0};
	std::list<Scheduler::InvocableWrapper> taskList_;
	mutable std::mutex mutex_;
};

template<typename Invocable>
void Scheduler::Co(Invocable&& func, pthread_t target_thread) {
	std::lock_guard<std::mutex> guard(mutex_);
	bool need_notify = AddTaskNoLock(std::forward<Invocable>(func), target_thread);
	if (need_notify) {
		Notify();
	}
}

template<typename Invocable>
bool Scheduler::AddTaskNoLock(Invocable&& func, pthread_t target_thread) {
	bool need_notify = false;
	if (taskList_.empty()) {
		need_notify = true;
	}

	taskList_.emplace_back(std::forward<Invocable>(func), target_thread);

	return need_notify;
}

} // namespace concurrency
} // namespace sylar
