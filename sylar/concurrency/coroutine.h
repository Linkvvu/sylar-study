#pragma once

#include <memory>
#include <functional>
#include <ucontext.h>

namespace sylar {
namespace concurrency {

class Scheduler;

class Coroutine final : public std::enable_shared_from_this<Coroutine> {
	friend Scheduler;
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
	/// @brief 获得当前线程在当前时刻正在执行或即将执行的协程
	static std::shared_ptr<Coroutine> GetCurCoroutine();

	void Reset(std::function<void()> func);

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
	/// @brief 设置 routine 为当前运行的协程
	static void SetCurCoroutine(Coroutine* routine);

	/// @brief 获取当前运行的协程指针
	// static std::shared_ptr<Coroutine> GetCurCoroutine();

	/// @brief Create a main coroutine for this thread.
	///		   It has no effect if it was already created.
	///
	///		   main协程使用当前线程的栈空间，故无需为其分配栈空间
	///		   main协程用以推进当前线程的执行流，故无需为其指定回调函数
	static void CreateMainCoroutine();

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
