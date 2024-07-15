#pragma once

#include <memory>
#include <functional>
#include <ucontext.h>

namespace sylar {
namespace concurrency {

class Scheduler;
class Coroutine;

namespace this_thread {

/// @brief Get the main coroutine of this thread,
///		   if it's not exist, create it.
///
///		   main协程使用当前线程的栈空间，故无需为其分配栈空间
///		   main协程以当前线程的执行流推进，故无需为其指定回调函数
Coroutine* GetMainCoroutine();

/// @brief 获得当前线程在当前时刻正在执行或即将执行的协程
std::shared_ptr<Coroutine> GetCurrentRunningCoroutine();

} // namespace this_thread

class Coroutine final : public std::enable_shared_from_this<Coroutine> {
	friend Scheduler;
	friend Coroutine* concurrency::this_thread::GetMainCoroutine();

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

	/// @brief 创建一个协程对象
	/// @param func  协程回调函数
	/// @param stack_size  为该协程对象分配的栈大小
	/// @pre 存在main_coroutine
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

	void Reset(std::function<void()> func);

	bool IsRunnable() const;

public:
	static void YieldCurCoroutineToHold();

	static void YieldCurCoroutineToReady();

private:
	/// @brief create a main coroutine for current thread
	/// @pre 这应当是当前线程第一次也是唯一一次调用
	Coroutine();

	void SetState(State s)
	{ state_ = s; }

	void DoFunc() const
	{ func_(); }

private:
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
