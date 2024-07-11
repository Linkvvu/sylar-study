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

/// @brief 无效协程ID：0
static std::atomic<cc::Coroutine::CoroutineId> s_coroutine_next_id = {1};
static std::atomic<int> s_coroutine_count = {0};

} // namespace

static auto sylar_logger = SYLAR_ROOT_LOGGER();

/// FIXME:
///		使用“局部作用域、静态thread_local的实例”
///	@code
/// 	thread_local static std::shared_ptr<cc::Coroutine> tl_sp_main_coroutine;
/// @endcode
void cc::Coroutine::CreateMainCoroutine() {
	if (__builtin_expect(tl_p_cur_coroutine == nullptr, 0)) {
		// create the main coroutine for this thread
		auto main_coroutine = std::shared_ptr<Coroutine>(new Coroutine);

		// sets it as the main coroutine for current thread
		tl_sp_main_coroutine = std::move(main_coroutine);

		// sets it as current coroutine for current thread
		Coroutine::SetCurCoroutine(tl_sp_main_coroutine.get());
	}
}

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
	s_coroutine_count.fetch_add(1, std::memory_order::memory_order_relaxed);
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
	s_coroutine_count.fetch_add(1, std::memory_order::memory_order_relaxed);
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
		Coroutine::SetCurCoroutine(nullptr);
	}

	// updates the coroutine counter
	s_coroutine_count.fetch_sub(1, std::memory_order::memory_order_relaxed);

    SYLAR_LOG_DEBUG(sylar_logger) << "Coroutine::~Coroutine id=" << GetId()
			<< ", total=" << s_coroutine_count.load(std::memory_order::memory_order_relaxed) << std::endl;
}

void cc::Coroutine::SwapIn() {
	/// TODO:
	/// @code SYLAR_ASSERT(GetCurCoroutine() == main-coroutine) @endcode
	SYLAR_ASSERT(Coroutine::GetCurCoroutine().get() == tl_sp_main_coroutine.get());
	SYLAR_ASSERT(GetState() != State::kExec);
	Coroutine::SetCurCoroutine(this);
	SetState(State::kExec);
	if (swapcontext(&tl_sp_main_coroutine.get()->ctx_, &this->ctx_)) {
		SYLAR_LOG_FATAL(sylar_logger) << "fail to invoke ::swapcontext, about to abort!" << std::endl;
		std::abort();
	}
}

void cc::Coroutine::SwapOut() {
	SYLAR_ASSERT(Coroutine::GetCurCoroutine().get() == this);
	Coroutine::SetCurCoroutine(tl_sp_main_coroutine.get());
	/// FIXME:
	/// 	should update state for the instance ?

	// Its state is set by the main coroutine to which it belongs,
	// swap out directly
	if (swapcontext(&this->ctx_, &tl_sp_main_coroutine.get()->ctx_)) {
		SYLAR_LOG_FATAL(sylar_logger) << "fail to invoke ::swapcontext, about to abort!" << std::endl;
		std::abort();
	}
}

void cc::Coroutine::SetCurCoroutine(Coroutine* routine) {
	tl_p_cur_coroutine = routine;
}

std::shared_ptr<cc::Coroutine> cc::Coroutine::GetCurCoroutine() {
	SYLAR_ASSERT_WITH_MSG(tl_p_cur_coroutine != nullptr
			, "current thread hasn't a main coroutine, create it first");
    return tl_p_cur_coroutine->shared_from_this();
}

void cc::Coroutine::Reset(std::function<void()> func) {
	SYLAR_ASSERT(stackFrame_ && stackSize_);
	SYLAR_ASSERT(GetState() == State::kTerminal
			|| GetState() == State::kExcept
			|| GetState() == State::kInit)
	func_ = std::move(func);

	ctx_ = {};
	if (::getcontext(&ctx_)) {
		SYLAR_LOG_FATAL(sylar_logger) << "fail to invoke ::getcontext, about to abort!" << std::endl;
		std::abort();
	}
	ctx_.uc_stack.ss_sp = stackFrame_;
	ctx_.uc_stack.ss_size = stackSize_;
	::makecontext(&ctx_, &Coroutine::CoroutineFunc, 0);

	state_ = State::kInit;
}

void cc::Coroutine::CoroutineFunc() {
	// get current coroutine to run (refer count +1)
	auto cur_coroutine = Coroutine::GetCurCoroutine();
	SYLAR_ASSERT(cur_coroutine != nullptr);
	SYLAR_ASSERT(cur_coroutine->GetState() == State::kExec);

	// invokes callback of current coroutine
	try {
		cur_coroutine->DoFunc();
		cur_coroutine->func_ = nullptr;
		cur_coroutine->SetState(State::kTerminal);
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
	cur_coroutine_raw_ptr->SwapOut();
}

void cc::Coroutine::YieldCurCoroutineToHold() {
	auto cur = GetCurCoroutine();
	SYLAR_ASSERT(cur->GetState() == State::kExec);
	cur->SetState(State::kHold);
	cur->SwapOut();
}

void cc::Coroutine::YieldCurCoroutineToReady() {
	auto cur = GetCurCoroutine();
	SYLAR_ASSERT(cur->GetState() == State::kExec);
	cur->SetState(State::kReady);
	cur->SwapOut();
}
