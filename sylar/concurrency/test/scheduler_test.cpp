#include <concurrency/scheduler.h>
#include <base/debug.h>
#include <iostream>
#include <thread>

using namespace sylar;

namespace cc = sylar::concurrency;

int main() {
	cc::Scheduler scheduler(3, false, "TestScheduler");
	scheduler.Start();
	scheduler.Co([]() {
			SYLAR_LOG_DEBUG(SYLAR_ROOT_LOGGER()) << "============" << std::endl;
			cc::Coroutine::YieldCurCoroutineToReady();
			std::cout << "x" << std::endl;
	});

	// scheduler.Co([]() {
	// 	SYLAR_LOG_DEBUG(SYLAR_ROOT_LOGGER()) << "************" << std::endl;
	// 	cc::Coroutine::YieldCurCoroutineToHold();
	// });
	std::this_thread::sleep_for(std::chrono::seconds(100));
}
