#include <concurrency/scheduler.h>
#include <concurrency/thread.h>

using namespace sylar;
namespace cc = sylar::concurrency;

namespace {

// /// @brief
// static thread_local cc::Scheduler* tl_scheduler = nullptr;

/// @brief 当前线程负责调度(任务)协程的(调度)协程对象
static thread_local cc::Coroutine* tl_schedule_coroutine = nullptr;

} // namespace

cc::Scheduler::Scheduler(size_t thread_num, bool include_cur_thread, std::string name)
	: name_(std::move(name))
	, rootCoroutine_(include_cur_thread
			? std::make_unique<cc::Coroutine>(
					std::bind(&Scheduler::ScheduleFunc, this), 0)
			: nullptr)
	, rootPthreadId_(include_cur_thread ? base::GetPthreadId() : 0)
	, threadPool_((include_cur_thread ? thread_num - 1 : thread_num))
	, stopped_(false)
{
	if (include_cur_thread) {
		cc::Coroutine::CreateMainCoroutine();
		::tl_schedule_coroutine = rootCoroutine_.get();
	}
}

cc::Scheduler::~Scheduler() noexcept {}

void cc::Scheduler::Start() {
	bool expected = true;
	if (stopped_.compare_exchange_strong(expected, false, std::memory_order::memory_order_release)) {
		for (size_t i = 0; i < threadPool_.size(); ++i) {
			threadPool_[i].reset(new cc::Thread(
				[this]() {
					this->ScheduleFunc();
				}, name_ + "_" + std::to_string(i)
			));
		}
	}
}

void cc::Scheduler::ScheduleFunc() {
	// create main coroutine for current (each) thread
	cc::Coroutine::CreateMainCoroutine();


	std::shared_ptr<cc::Coroutine> current_task(nullptr, 0);
	while (!stopped_.load(std::memory_order::memory_order_acquire)) {
		current_task.reset();

		std::lock_guard<std::mutex> guard(mutex_);
		while (taskList_.empty())

	}
}

cc::Scheduler::Invokable::Invokable(const std::shared_ptr<cc::Coroutine>& co, ::pthread_t pthread_id)
	: target_thread(pthread_id)
	, coroutine(co)
	{}

cc::Scheduler::Invokable::Invokable(std::shared_ptr<cc::Coroutine>&& co, ::pthread_t pthread_id)
	: target_thread(pthread_id)
	, coroutine(std::move(co))
	{}

cc::Scheduler::Invokable::Invokable(const std::function<void()>& cb, ::pthread_t pthread_id)
	: target_thread(pthread_id)
	, callback(cb)
	{}

cc::Scheduler::Invokable::Invokable(std::function<void()>&& cb, ::pthread_t pthread_id)
	: target_thread(pthread_id)
	, callback(std::move(cb))
	{}

void cc::Scheduler::Invokable::Reset() {
	this->target_thread = 0;
	this->callback = nullptr;
	this->coroutine.reset();
}
