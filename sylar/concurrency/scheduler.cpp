#include <concurrency/scheduler.h>
#include <concurrency/thread.h>
#include <concurrency/notifier.h>
#include <concurrency/epoll_poller.h>
#include <concurrency/timer_manager.h>
#include <concurrency/hook.h>
#include <base/debug.h>

using namespace sylar;
namespace cc = sylar::concurrency;

namespace sylar {
namespace concurrency {
namespace this_thread {

/// @brief 当前线程所属的Scheduler实例
static thread_local cc::Scheduler* tl_scheduler = nullptr;

/// @brief 当前线程负责调度(任务)协程的(调度)协程对象
static thread_local cc::Coroutine* tl_scheduling_coroutine = nullptr;

static void SetSchedulingCoroutine(cc::Coroutine* co) {
	if (co) {
		SYLAR_ASSERT_WITH_MSG(tl_scheduling_coroutine == nullptr,
				"current already has a scheduling coroutine");
	}
	tl_scheduling_coroutine = co;
}

static void SetScheduler(cc::Scheduler* scheduler) {
	if (scheduler) {
		SYLAR_ASSERT(tl_scheduler == nullptr);
	}
	tl_scheduler = scheduler;
}

Coroutine* GetSchedulingCoroutine() {
    return tl_scheduling_coroutine;
}

Scheduler* GetScheduler() {
    return tl_scheduler;
}

} // namespace this_thread
} // namespace concurrency
} // namespace sylar


cc::Scheduler::Scheduler(size_t thread_num, bool include_cur_thread, std::string name)
	: name_(std::move(name))
	, dummyMainCoroutine_(nullptr)
	, dummyMainTrdPthreadId_(include_cur_thread ? base::GetPthreadId() : INVALID_PTHREAD_ID)
	, poller_(std::make_unique<cc::EpollPoller>(this))
	, threadPool_((include_cur_thread ? thread_num - 1 : thread_num))
{
	if (include_cur_thread) {
		cc::this_thread::GetMainCoroutine();
		cc::this_thread::SetSchedulingCoroutine(dummyMainCoroutine_.get());
		dummyMainCoroutine_ = std::make_shared<cc::Coroutine>(std::bind(&Scheduler::SchedulingFunc, this), 1024 * 10, true);
	}
}

cc::Scheduler::~Scheduler() noexcept {
	SYLAR_ASSERT(this->IsStopped());
}

void cc::Scheduler::Start() {
	bool expected = true;
	if (stopped_.compare_exchange_strong(expected, false, std::memory_order::memory_order_acq_rel)) {
		for (size_t i = 0; i < threadPool_.size(); ++i) {
			threadPool_[i].reset(new cc::Thread(
				[this]() {
					this->SchedulingFunc();
				}, name_ + "_" + std::to_string(i)
			));
		}
	}
}

void cc::Scheduler::Stop() {
	if (dummyMainCoroutine_) {
		SYLAR_ASSERT_WITH_MSG(base::GetPthreadId() == this->dummyMainTrdPthreadId_,
			"only can invoke Scheduler::Stop by the thread creating"
			"the Scheduler instance when enable dummy-main");
	}

	bool expected = false;
	if (!stopped_.compare_exchange_strong(expected, true, std::memory_order::memory_order_acq_rel)) {
		return;
	}

	Notify(threadPool_.size());

	if (dummyMainCoroutine_) {
		Notify();
	}

	if (dummyMainCoroutine_) {
		dummyMainCoroutine_->SwapIn();
	}

	std::vector<std::unique_ptr<concurrency::Thread>> tmp_pool;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		tmp_pool.swap(this->threadPool_);
	}

	for (size_t i = 0; i < tmp_pool.size(); ++i) {
		tmp_pool[i]->Join();
	}
}

bool cc::Scheduler::IsStopped() const {
	std::lock_guard<std::mutex> guard(mutex_);
    return stopped_ && taskList_.empty();
}

void cc::Scheduler::SchedulingFunc() {
	// set the scheduler(this)
	cc::this_thread::SetScheduler(this);

	cc::this_thread::EnableHook(true);

	// create main coroutine for current (each) thread
	auto scheduling_coroutine = cc::this_thread::GetMainCoroutine();

	if (this->dummyMainCoroutine_ && base::GetPthreadId() == this->dummyMainTrdPthreadId_) {
		// set dummy-routine as the scheduling coroutine of this thread
		cc::this_thread::SetSchedulingCoroutine(dummyMainCoroutine_.get());
		SYLAR_ASSERT(this->dummyMainCoroutine_ == this_thread::GetCurrentRunningCoroutine());
		SYLAR_ASSERT(this->dummyMainCoroutine_.get() != cc::this_thread::GetMainCoroutine());
	} else {
		// set main-routine of current thread as the scheduling coroutine of this thread
		cc::this_thread::SetSchedulingCoroutine(scheduling_coroutine);
		SYLAR_ASSERT(cc::this_thread::GetSchedulingCoroutine() == cc::this_thread::GetMainCoroutine());
	}

	// create idle_coroutine to handle idle event
	auto idle_coroutine = std::make_shared<cc::Coroutine>(std::bind(&Scheduler::HandleIdle, this));

	// pointer to the temp coroutine, to wrap a callback be executed as a coroutine
	std::shared_ptr<cc::Coroutine> temp_coroutine;

	InvocableWrapper current_task {};
	while (true) {
		bool has_task = false;
		bool need_notify = false;
		current_task.Reset();
		{
			std::lock_guard<std::mutex> guard(mutex_);
			decltype(taskList_)::iterator it;
			for (it = taskList_.begin(); it != taskList_.end(); ++it) {
				if (it->target_thread != INVALID_PTHREAD_ID
						&& it->target_thread != base::GetPthreadId())
				{
					need_notify = true;
					continue;
				}

				SYLAR_ASSERT(it->coroutine || it->callback);
				/// FIXME:
				///		concurrency::Coroutine并不满足线程安全结构，
				///		因此一个协程对象同一时刻，只能被一个线程所执行。
				///		pending列表类型应该由vector->set
				/// 	以下断言并不能避免协程被多个线程执行，因为该协程
				///     或许被其他scheduling coroutine获取，并即将执行，
				///		只是还未改变状态
				if (it->coroutine) {
					SYLAR_ASSERT(it->coroutine->GetState() != Coroutine::State::kExec);
				}
				current_task = std::move(*it);
				taskList_.erase(it);
				has_task = true;
				break;
			}

			need_notify = need_notify || !taskList_.empty();
		}

		if (need_notify) {
			// 只通知一个线程获取任务，避免多个线程竞争锁，并将唤醒的责任移交给它
			Notify();
		}

		if (current_task.coroutine &&
				(current_task.coroutine->GetState() != cc::Coroutine::State::kTerminal
				|| current_task.coroutine->GetState() != cc::Coroutine::State::kExcept))
		{
			++activeThreadNum_;
			current_task.coroutine->SwapIn();
			--activeThreadNum_;

			if (current_task.coroutine->GetState() == cc::Coroutine::State::kReady) {
				// add to list again (need a lock here, optimize it)
				Co(std::move(current_task.coroutine));
			} else if (current_task.coroutine->GetState() != cc::Coroutine::State::kTerminal
					&& current_task.coroutine->GetState() != cc::Coroutine::State::kExcept)
			{
				/// TODO:
				/// 	协程的状态交由用户管理，对于非法状态应当报错或警告
				current_task.coroutine->SetState(cc::Coroutine::State::kHold);
			}
		} else if (current_task.callback) {
			if (temp_coroutine) {
				temp_coroutine->Reset(std::move(current_task.callback));
			} else {
				temp_coroutine = std::make_shared<cc::Coroutine>(std::move(current_task.callback));
			}
			// invoke it by warp it o a coroutine
			++activeThreadNum_;
			temp_coroutine->SwapIn();
			--activeThreadNum_;

			if (temp_coroutine->GetState() == cc::Coroutine::State::kReady) {
				// add to list again (need a lock here, optimize it)
				Co(std::move(temp_coroutine));
			} else if (temp_coroutine->GetState() == cc::Coroutine::State::kTerminal
					|| temp_coroutine->GetState() == cc::Coroutine::State::kExcept)
			{
				temp_coroutine->Reset(nullptr);
			} else {
				temp_coroutine->SetState(cc::Coroutine::State::kHold);
				temp_coroutine.reset();
			}
		} else {
			if (has_task) {
				continue;
			}

			if (idle_coroutine->GetState() == cc::Coroutine::State::kTerminal) {
				SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "idle coroutine is terminal" << std::endl;
				break;
			}

			++idleThreadNum_;
			idle_coroutine->SwapIn();
			--idleThreadNum_;
			if (idle_coroutine->GetState() != cc::Coroutine::State::kTerminal
					&& idle_coroutine->GetState() != cc::Coroutine::State::kExcept)
			{
				idle_coroutine->SetState(cc::Coroutine::State::kHold);
			}
		}
	}

	cc::this_thread::SetSchedulingCoroutine(nullptr);
	cc::this_thread::SetScheduler(nullptr);
}

void cc::Scheduler::HandleIdle() {
	SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "Scheduler::HandleIdle is invoked" << std::endl;

	while (!IsStopped()) {
		poller_->PollAndHandle();

		// swap to the scheduling coroutine
		cc::Coroutine::YieldCurCoroutineToHold();
	}
}

void cc::Scheduler::Notify(uint64_t num) {
	poller_->GetNotifier()->Notify(num);
}

void cc::Scheduler::AssertInSchedulingScope() const {
	SYLAR_ASSERT_WITH_MSG(this == cc::this_thread::GetScheduler(),
		"runs outside the scheduling scope");
}

void cc::Scheduler::AppendEvent(int fd, unsigned interest_events, std::function<void()> func) {
	poller_->AppendEvent(fd, interest_events, std::move(func));
}

void cc::Scheduler::UpdateEvent(int fd, unsigned interest_events, std::function<void()> func) {
	// AssertInSchedulingScope();
	poller_->UpdateEvent(fd, interest_events, std::move(func));
}

void cc::Scheduler::CancelEvent(int fd, unsigned target_events) {
	// AssertInSchedulingScope();
	poller_->CancelEvent(fd, target_events);
}

uint32_t cc::Scheduler::RunAt(std::chrono::steady_clock::time_point tp, std::function<void()> cb) {
	Timer::TimerId id = poller_->GetTimerManager()->GetNextTimerId();
	Timer new_timer(id, std::move(tp), cc::Timer::Interval::zero(), std::move(cb));
	poller_->GetTimerManager()->AddTimer(std::move(new_timer));
	return id;
}

uint32_t sylar::concurrency::Scheduler::RunAtIf(std::chrono::steady_clock::time_point tp, std::weak_ptr<void> cond, std::function<void()> cb) {
	Timer::TimerId id = poller_->GetTimerManager()->GetNextTimerId();
	Timer new_timer(id, std::move(tp), cc::Timer::Interval::zero(), std::move(cb));
	poller_->GetTimerManager()->AddConditionTimer(std::move(new_timer), std::move(cond));
    return id;
}

bool sylar::concurrency::Scheduler::HasTimer(uint32_t timer_id) {
    return poller_->GetTimerManager()->HasTimer(timer_id);
}

uint32_t cc::Scheduler::RunAfter(std::chrono::steady_clock::duration dur, std::function<void()> cb, bool repeated) {
	auto tp = std::chrono::steady_clock::now() + dur;
	if (repeated) {
		Timer::TimerId id = poller_->GetTimerManager()->GetNextTimerId();
		Timer new_timer(id, std::move(tp), std::move(dur), std::move(cb));
		poller_->GetTimerManager()->AddTimer(std::move(new_timer));
		return id;
	}
    return RunAt(tp, std::move(cb));
}

uint32_t sylar::concurrency::Scheduler::RunAfterIf(std::chrono::steady_clock::duration dur, std::weak_ptr<void> cond, std::function<void()> cb, bool repeated) {
	auto tp = std::chrono::steady_clock::now() + dur;
	if (repeated) {
		Timer::TimerId id = poller_->GetTimerManager()->GetNextTimerId();
		Timer new_timer(id, std::move(tp), std::move(dur), std::move(cb));
		poller_->GetTimerManager()->AddConditionTimer(std::move(new_timer), std::move(cond));
		return id;
	}
    return RunAtIf(tp, std::move(cond), std::move(cb));
}

void sylar::concurrency::Scheduler::CancelTimer(uint32_t timer_id) {
	poller_->GetTimerManager()->CancelTimer(timer_id);
}

cc::Scheduler::InvocableWrapper::InvocableWrapper(const std::shared_ptr<cc::Coroutine>& co, ::pthread_t pthread_id)
	: target_thread(pthread_id)
	, coroutine(co)
	{}

cc::Scheduler::InvocableWrapper::InvocableWrapper(std::shared_ptr<cc::Coroutine>&& co, ::pthread_t pthread_id)
	: target_thread(pthread_id)
	, coroutine(std::move(co))
	{}

cc::Scheduler::InvocableWrapper::InvocableWrapper(const std::function<void()>& cb, ::pthread_t pthread_id)
	: target_thread(pthread_id)
	, callback(cb)
	{}

cc::Scheduler::InvocableWrapper::InvocableWrapper(std::function<void()>&& cb, ::pthread_t pthread_id)
	: target_thread(pthread_id)
	, callback(std::move(cb))
	{}

void cc::Scheduler::InvocableWrapper::Reset() {
	this->target_thread = 0;
	this->callback = nullptr;
	this->coroutine.reset();
}
