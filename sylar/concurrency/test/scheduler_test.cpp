#include <concurrency/scheduler.h>
#include <base/debug.h>
#include <iostream>
#include <thread>

using namespace sylar;

namespace cc = sylar::concurrency;

void Test_CoroutineFunc1() {
	static int s_count = 5;
	SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "test in func1 coroutine s_count=" << s_count << std::endl;

	std::this_thread::sleep_for(std::chrono::seconds(1));

	if (s_count-- > 0) {
		cc::this_thread::GetScheduler()->Co(&Test_CoroutineFunc1, base::GetPthreadId());
	}
}

void Test_CoroutineFunc2() {
	static int s_count = 5;
	SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "test in func2 coroutine s_count=" << s_count << std::endl;

	std::this_thread::sleep_for(std::chrono::seconds(1));

	if (s_count-- > 0) {
		cc::this_thread::GetScheduler()->Co(&Test_CoroutineFunc2, base::GetPthreadId());
	}
}


void OnlyMainDoScheduleTest() {
	cc::Scheduler scheduler(1, true, "TestMainScheduler");
	scheduler.Start();
	scheduler.Co(Test_CoroutineFunc2);
	scheduler.Stop();
}

int main() {
	cc::Scheduler scheduler(3, false, "TestScheduler");
	scheduler.Start();
	scheduler.Co(&Test_CoroutineFunc1);
	scheduler.Stop();

	std::cout << "==================================" << std::endl;

	OnlyMainDoScheduleTest();
}
