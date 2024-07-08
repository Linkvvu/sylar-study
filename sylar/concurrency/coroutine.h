#pragma once

#include <memory>
#include <functional>
#include <ucontext.h>

namespace sylar {
namespace concurrency {

class Coroutine final : public std::enable_shared_from_this<Coroutine> {
public:
	using CoroutineId = uint32_t;

	enum class State : uint8_t {
		kInit,
		kExec,
		kHold,
		kReady,
		kTerminal,
		kExcept
	};

	explicit Coroutine(std::function<void()> func, uint32_t stack_size = 1024 * 1024);

	~Coroutine() noexcept;

	/// @brief 将当前协程换入CPU
	/// @pre GetState() != kExec
	/// @pre GetState() == kExec
	void SwapIn();

	/// @brief 将当前协程换出CPU
	void SwapOut();

	State GetState() const
	{ return state_; }

	CoroutineId GetId() const
	{ return id_; }

	/// FIXME: as private
	static std::shared_ptr<Coroutine> GetNowCoroutine();
private:
	/// @brief create a main coroutine for current thread
	/// @pre 这应当是当前线程第一次也是唯一一次调用
	Coroutine();

	void SetState(State s)
	{ state_ = s; }

	void ResetFunc()
	{ func_ = nullptr; }

	void DoFunc() const
	{ func_(); }

private:
	/// @brief 设置 routine 为当前运行的协程
	static void SetNowCoroutine(Coroutine* routine);

	/// @brief 获取当前运行的协程指针
	// static std::shared_ptr<Coroutine> GetNowCoroutine();

	/// @brief 协程入口函数
	static void CoroutineFunc();

private:
	std::function<void()> func_ = nullptr;
	void* stackFrame_ = nullptr;
	uint32_t stackSize_ = 0;
	CoroutineId id_;
	::ucontext_t ctx_ = {};
	State state_;
};

} // namespace concurrency
} // namespace sylar
