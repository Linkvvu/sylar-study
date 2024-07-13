#include <concurrency/coroutine.h>
#include <base/log.h>

using namespace sylar;

static auto logger = SYLAR_ROOT_LOGGER();

void co1_func() {
	SYLAR_LOG_DEBUG(logger) << "co1 func first" << std::endl;
	concurrency::this_thread::GetCurCoroutine()->SwapOut();
	SYLAR_LOG_DEBUG(logger) << "co1 func second" << std::endl;
	concurrency::this_thread::GetCurCoroutine()->SwapOut();
	SYLAR_LOG_DEBUG(logger) << "co1 func third" << std::endl;
	concurrency::this_thread::GetCurCoroutine()->SwapOut();
	SYLAR_LOG_DEBUG(logger) << "co1 func here never reach" << std::endl;
}

int main() {
	concurrency::this_thread::GetCurCoroutine();

	SYLAR_LOG_DEBUG(logger) << "main begin" << std::endl;
	auto co1 = std::make_shared<concurrency::Coroutine>(&co1_func);
	SYLAR_LOG_DEBUG(logger) << "about to swapin first" << std::endl;
	co1->SwapIn();
	SYLAR_LOG_DEBUG(logger) << "about to swapin second" << std::endl;
	co1->SwapIn();
	SYLAR_LOG_DEBUG(logger) << "about to swapin last" << std::endl;
	co1->SwapIn();
}
