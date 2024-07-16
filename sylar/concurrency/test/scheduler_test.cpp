#include <concurrency/scheduler.h>
#include <base/debug.h>
#include <iostream>
#include <thread>

using namespace sylar;

namespace cc = sylar::concurrency;

void Test_CoroutineFunc() {
	static int s_count = 5;
	SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "test in coroutine s_count=" << s_count << std::endl;

	std::this_thread::sleep_for(std::chrono::seconds(1));

	if (s_count-- > 0) {
		cc::this_thread::GetScheduler()->Co(&Test_CoroutineFunc, base::GetPthreadId());
	}
}

int main() {
	cc::Scheduler scheduler(3, false, "TestScheduler");
	scheduler.Start();
	scheduler.Co(&Test_CoroutineFunc);
	scheduler.Stop();
}
