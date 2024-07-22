#include <concurrency/hook.h>
#include <concurrency/scheduler.h>
#include <base/log.h>

#include <thread>
#include <csignal>
#include <unistd.h>

using namespace sylar;
namespace cc = sylar::concurrency;

bool stopped = false;

int main() {
	std::signal(SIGINT, [](int s) {
		stopped = true;
	});

	cc::Scheduler scheduler(1, true, "Hook_Scheduler");
	scheduler.Co([]() {
		// std::this_thread::sleep_for(std::chrono::seconds(3)); no support
		sleep(3);
		SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "I'm back now" << std::endl;
	});
	scheduler.Co([]() {
		while (!stopped) {
			// busy
		}
	});
	scheduler.Start();
	scheduler.Stop();
}
