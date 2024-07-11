#include <concurrency/scheduler.h>
#include <concurrency/thread.h>
#include <base/debug.h>

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
	, stopped_(true)
	, activeThreadNum_(0)
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

bool cc::Scheduler::IsStopped() const {
	std::lock_guard<std::mutex> guard(mutex_);
    return stopped_ && taskList_.empty() && activeThreadNum_ == 0;
}

void cc::Scheduler::ScheduleFunc() {
	// create main coroutine for current (each) thread
	cc::Coroutine::CreateMainCoroutine();

	std::shared_ptr<cc::Coroutine> idle_coroutine
			= std::make_shared<cc::Coroutine>(std::bind(&Scheduler::HandleIdle, this));
	std::shared_ptr<cc::Coroutine> temp_coroutine;
	InvocableWrapper current_task {};

	while (true) {
		current_task.Reset();
		// bool need_notify = false;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			decltype(taskList_)::iterator it;
			for (it = taskList_.begin(); it != taskList_.end(); ++it) {
				if (it->target_thread != 0
						&& it->target_thread != base::GetPthreadId())
				{
					// need_notify = true;
					continue;
				}

				/// FIXME:
				///		maybe assert this
				///	@code
				/// 	SYLAR_ASSERT(it->coroutine
				///			&& it->coroutine->GetState() != Coroutine::State::kExec);
				/// @endcode
				SYLAR_ASSERT(it->coroutine || it->callback);

				if (it->coroutine && it->coroutine->GetState() == Coroutine::State::kExec) {
					continue;
				}

				current_task = std::move(*it);
				taskList_.erase(it);
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
		// swap to schedule coroutine
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
