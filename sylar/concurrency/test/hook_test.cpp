#include <concurrency/hook.h>
#include <concurrency/scheduler.h>
#include <base/log.h>

#include <thread>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

using namespace sylar;
namespace cc = sylar::concurrency;

bool stopped = false;

int sock;

char buffer[1024 * 4] {};

void DoConnect(cc::Scheduler* scheduler) {
	sock = ::socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    ::inet_pton(AF_INET, "204.79.197.200", &addr.sin_addr.s_addr);

    int ret = connect(sock, (const sockaddr*)&addr, sizeof(addr));
	if (ret == -1) {
		if (errno == EINPROGRESS) {
			SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "add write event" << std::endl;
			scheduler->UpdateEvent(sock, EPOLLOUT, [scheduler]() {
				SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "writeable callback" << std::endl;

				auto num = read(sock, buffer, sizeof buffer);
				SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "read " << num << " bytes" << std::endl;

				scheduler->RunAfter(std::chrono::milliseconds(500), [scheduler]() {
					SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "read data: " << buffer << std::endl;
				});
				close(sock);
			});
		}
	}
}

int main() {
	std::signal(SIGINT, [](int s) {
		stopped = true;
	});

	cc::Scheduler scheduler(1, false, "Hook_Scheduler");
	scheduler.Co([]() {
		// std::this_thread::sleep_for(std::chrono::seconds(3)); no support
		sleep(3);
		SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "I'm back now" << std::endl;
	});
	scheduler.Co(std::bind(DoConnect, &scheduler));
	scheduler.Start();

	std::this_thread::sleep_for(std::chrono::seconds(10));
	scheduler.Stop();
}
