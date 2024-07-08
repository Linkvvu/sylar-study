#include <base/debug.h>
#include <concurrency/coroutine.h>

#include <atomic>
#include <memory>
#include "coroutine.h"

using namespace sylar;

namespace cc = concurrency;

namespace {

/// @brief 当前线程中正在(即将)运行的协程
thread_local static cc::Coroutine* tl_p_cur_coroutine = nullptr;

/// @brief 当前线程中的主协程(第一个被构造的协程)
thread_local static std::shared_ptr<cc::Coroutine> tl_sp_main_coroutine = nullptr;

static std::atomic<cc::Coroutine::CoroutineId> s_coroutine_next_id = {0};
static std::atomic<size_t> s_coroutine_count = {0};

} // namespace

static auto sylar_logger = SYLAR_ROOT_LOGGER();

cc::Coroutine::Coroutine()
	: id_(s_coroutine_next_id.fetch_add(1, std::memory_order::memory_order_relaxed))
	, state_(State::kExec)
{
	SYLAR_ASSERT(tl_sp_main_coroutine == nullptr);
	SYLAR_ASSERT(tl_p_cur_coroutine == nullptr);

	if (::getcontext(&ctx_)) {
		SYLAR_LOG_FATAL(sylar_logger) << "fail to invoke ::getcontext, about to abort!" << std::endl;
		std::abort();
	}

	/// FIXME:
	///		@code tl_sp_main_coroutine.reset(this); @endcode

	// updates counter
	s_coroutine_count.store(1, std::memory_order::memory_order_relaxed);
	SYLAR_LOG_FMT_DEBUG(sylar_logger, "main coroutine was constructed, id=%u\n", id_);
}

cc::Coroutine::Coroutine(std::function<void()> func, uint32_t stack_size)
	: func_(std::move(func))
	, stackFrame_(::malloc(stack_size))	///> FixMe
	, stackSize_(stack_size)
	, id_(s_coroutine_next_id.fetch_add(1, std::memory_order::memory_order_relaxed))
	, state_(State::kInit)
{
	SYLAR_ASSERT(func_ != nullptr);
	SYLAR_ASSERT(stackFrame_ != nullptr);
	// gets context
	if (::getcontext(&ctx_)) {
		SYLAR_LOG_FATAL(sylar_logger) << "fail to invoke ::getcontext, about to abort!" << std::endl;
		std::abort();
	}

	// pads fields for context
	ctx_.uc_link = nullptr;
	ctx_.uc_stack.ss_sp = stackFrame_;
	ctx_.uc_stack.ss_size = stackSize_;

	// makes context
	::makecontext(&ctx_, &Coroutine::CoroutineFunc, 0);

	// updates counter
	s_coroutine_count.store(1, std::memory_order::memory_order_relaxed);
	SYLAR_LOG_FMT_DEBUG(sylar_logger, "worker coroutine was constructed, id=%u\n", id_);
}

cc::Coroutine::~Coroutine() noexcept {
	if (stackFrame_) {
		SYLAR_ASSERT(GetState() == State::kTerminal
				|| GetState() == State::kExec
				|| GetState() == State::kInit);

		// dealloc the associated stack
		::free(stackFrame_);
	} else {
        SYLAR_ASSERT(func_ == nullptr);
		SYLAR_ASSERT(GetState() == State::kExec);
		SYLAR_ASSERT(tl_p_cur_coroutine == this);
		Coroutine::SetNowCoroutine(nullptr);
	}

	// updates the coroutine counter
	s_coroutine_count.fetch_sub(1, std::memory_order::memory_order_relaxed);

    SYLAR_LOG_DEBUG(sylar_logger) << "Fiber::~Fiber id=" << GetId()
			<< " total=" << s_coroutine_count << std::endl;
}

void cc::Coroutine::SwapIn() {
	/// TODO:
	/// @code SYLAR_ASSERT(GetNowCoroutine() == main-coroutine) @endcode
	SYLAR_ASSERT(Coroutine::GetNowCoroutine().get() == tl_sp_main_coroutine.get());
	SYLAR_ASSERT(GetState() != State::kExec);
	Coroutine::SetNowCoroutine(this);
	SetState(State::kExec);
	if (swapcontext(&tl_sp_main_coroutine.get()->ctx_, &this->ctx_)) {
		SYLAR_LOG_FATAL(sylar_logger) << "fail to invoke ::swapcontext, about to abort!" << std::endl;
		std::abort();
	}
}

void cc::Coroutine::SwapOut() {
	SYLAR_ASSERT(Coroutine::GetNowCoroutine().get() == this);
	SYLAR_ASSERT(GetState() == State::kExec);
	Coroutine::SetNowCoroutine(tl_sp_main_coroutine.get());
	/// FIXME:
	/// 	should update state for the instance ?
	SetState(State::kHold);
	if (swapcontext(&this->ctx_, &tl_sp_main_coroutine.get()->ctx_)) {
		SYLAR_LOG_FATAL(sylar_logger) << "fail to invoke ::swapcontext, about to abort!" << std::endl;
		std::abort();
	}
}

void cc::Coroutine::SetNowCoroutine(Coroutine* routine) {
	tl_p_cur_coroutine = routine;
}

std::shared_ptr<cc::Coroutine> cc::Coroutine::GetNowCoroutine() {
	if (tl_p_cur_coroutine == nullptr) {
		// create the main coroutine for this thread
		auto main_coroutine = std::shared_ptr<Coroutine>(new Coroutine);

		// sets it as the main coroutine for current thread
		tl_sp_main_coroutine = std::move(main_coroutine);

		// sets it as current coroutine for current thread
		Coroutine::SetNowCoroutine(tl_sp_main_coroutine.get());

		return Coroutine::GetNowCoroutine();
	}

    return tl_p_cur_coroutine->shared_from_this();
}

void cc::Coroutine::CoroutineFunc() {
	// get current coroutine to run (refer count +1)
	auto cur_coroutine = Coroutine::GetNowCoroutine();
	SYLAR_ASSERT(cur_coroutine != nullptr);
	SYLAR_ASSERT(cur_coroutine->GetState() == State::kExec);

	// invokes callback of current coroutine
	try {
		cur_coroutine->DoFunc();
		cur_coroutine->ResetFunc();
	} catch (const std::exception& e) {
		cur_coroutine->SetState(State::kExcept);
		SYLAR_LOG_FMT_ERROR(sylar_logger,
				"Coroutine %u caught a std exception: %s\nBacktrace:\n%s\n",
				cur_coroutine->GetId(), e.what(), base::BacktraceToString(2, "\t").c_str());
	} catch (...) {
		cur_coroutine->SetState(State::kExcept);
		SYLAR_LOG_FMT_ERROR(sylar_logger,
				"Coroutine %u caught a unknown exception\nBacktrace:\n%s\n",
				cur_coroutine->GetId(), base::BacktraceToString(2, "\t").c_str());
	}

	// release the shared_ptr our just got
	auto cur_coroutine_raw_ptr = cur_coroutine.get();
	cur_coroutine.reset();

    SYLAR_ASSERT_WITH_MSG(false, "never reach, coroutine id=" + std::to_string(cur_coroutine_raw_ptr->GetId()));

	// cur_coroutine_raw_ptr->SwapOut();
}
