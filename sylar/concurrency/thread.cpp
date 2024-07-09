#include <base/debug.h>
#include <concurrency/thread.h>

using namespace sylar;

namespace  {
	thread_local static concurrency::Thread* tl_p_this_thread = nullptr;

	thread_local static std::string tl_thread_name = "UNDEFINED";

	static void SetThreadName(std::string name) {
		SYLAR_ASSERT_WITH_MSG(tl_thread_name == "UNDEFINED", "thread cannot be set repeatedly");
		tl_thread_name = std::move(name);
	}

	static void SetThisThread(concurrency::Thread* p_t) {
		SYLAR_ASSERT(tl_p_this_thread == nullptr);
		tl_p_this_thread = p_t;
	}
} // namespace

concurrency::Thread* concurrency::Thread::GetThisThread() {
	return tl_p_this_thread;
}

concurrency::Thread::Thread(std::function<void()> func, std::string name)
	: name_(std::move(name))
	, func_(std::move(func))
	, thread_(std::make_unique<std::thread>([this]() { this->ThreadFunc(); }))
{
	auto ensure_ready = ready_.get_future();
	ensure_ready.wait();

	SYLAR_LOG_FMT_DEBUG(SYLAR_ROOT_LOGGER(),
		"thread [%s] is constructed successfully\n",
		GetName().c_str());
}

concurrency::Thread::~Thread() noexcept {
	this->Detach();
	this->thread_.reset();
	::SetThisThread(nullptr);
}

void concurrency::Thread::Join() {
	if (thread_->joinable()) {
		thread_->join();
	}
}

void concurrency::Thread::Detach() {
	if (thread_->joinable()) {
		thread_->detach();
	}
}

void concurrency::Thread::ThreadFunc() {
	this->tid_ = base::GetTid();
	this->pthreadId_ = base::GetPthreadId();
	::SetThreadName(name_);
	::SetThisThread(this);

	// notify constructor that ready
	ready_.set_value();

	SYLAR_LOG_FMT_INFO(SYLAR_ROOT_LOGGER(),
		"thread [%s] starts running\n",
		GetName().c_str());

	func_();
}
