#include <concurrency/scheduler.h>
#include <concurrency/thread.h>
#include <base/debug.h>

using namespace sylar;
namespace cc = sylar::concurrency;

namespace {

// /// @brief
// static thread_local cc::Scheduler* tl_scheduler = nullptr;

/// @brief 当前线程负责调度(任务)协程的(调度)协程对象
static thread_local cc::Coroutine* tl_scheduling_coroutine = nullptr;

static void SetSchedulingCoroutine(cc::Coroutine* co) {
	SYLAR_ASSERT_WITH_MSG(tl_scheduling_coroutine == nullptr && co != nullptr,
			"current already has a scheduling coroutine");
	tl_scheduling_coroutine = co;
}

} // namespace

cc::Scheduler::Scheduler(size_t thread_num, bool include_cur_thread, std::string name)
	: name_(std::move(name))
	, rootCoroutine_(include_cur_thread
			? std::make_unique<cc::Coroutine>(
					std::bind(&Scheduler::ScheduleFunc, this), 0)
			: nullptr)
	, rootPthreadId_(include_cur_thread ? base::GetPthreadId() : INVALID_PTHREAD_ID)
	, threadPool_((include_cur_thread ? thread_num - 1 : thread_num))
	, stopped_(true)
	, activeThreadNum_(0)
{
	if (include_cur_thread) {
		cc::this_thread::GetMainCoroutine();
		::SetSchedulingCoroutine(rootCoroutine_.get());
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

bool cc::Scheduler::IsStopped() const {
	std::lock_guard<std::mutex> guard(mutex_);
    return stopped_ && taskList_.empty() && activeThreadNum_ == 0;
}

void cc::Scheduler::ScheduleFunc() {
	// create main coroutine for current (each) thread
	auto scheduling_coroutine = cc::this_thread::GetMainCoroutine().get();

	// set self as the scheduling coroutine of this thread
	::SetSchedulingCoroutine(scheduling_coroutine);

	// create idle_coroutine to handle idle event
	auto idle_coroutine = std::make_shared<cc::Coroutine>(std::bind(&Scheduler::HandleIdle, this));

	// pointer to the temp coroutine, to wrap a callback be executed as a coroutine
	std::shared_ptr<cc::Coroutine> temp_coroutine;

	InvocableWrapper current_task {};
	while (true) {
		bool has_task = false;
		current_task.Reset();
		// bool need_notify = false;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			decltype(taskList_)::iterator it;
			for (it = taskList_.begin(); it != taskList_.end(); ++it) {
				if (it->target_thread != INVALID_PTHREAD_ID
						&& it->target_thread != base::GetPthreadId())
				{
					// need_notify = true;
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
				SYLAR_ASSERT(it->coroutine && it->coroutine->GetState() != Coroutine::State::kExec);
				current_task = std::move(*it);
				taskList_.erase(it);
				has_task = true;
				break;
			}
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
					&& temp_coroutine->GetState() == cc::Coroutine::State::kExcept)
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

			++activeThreadNum_;
			idle_coroutine->SwapIn();
			--activeThreadNum_;
			if (idle_coroutine->GetState() != cc::Coroutine::State::kTerminal
					&& idle_coroutine->GetState() != cc::Coroutine::State::kExcept)
			{
				idle_coroutine->SetState(cc::Coroutine::State::kHold);
			}
		}
	}
}

void cc::Scheduler::HandleIdle() {
	SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "Scheduler::HandleIdle is invoked" << std::endl;

	while (!IsStopped()) {
		// swap to the scheduling coroutine
		cc::Coroutine::YieldCurCoroutineToHold();
	}
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

cc::Coroutine* cc::this_thread::GetSchedulingCoroutine() {
    return tl_scheduling_coroutine;
}
