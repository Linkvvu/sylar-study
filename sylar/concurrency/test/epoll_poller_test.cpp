#include <concurrency/epoll_poller.h>
#include <concurrency/scheduler.h>
#include <base/log.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

using namespace sylar;
namespace cc = sylar::concurrency;

int sock;

uint32_t timer_id = 0;

void DoConnect(cc::Scheduler* scheduler) {
	sock = ::socket(AF_INET, SOCK_STREAM, 0);
    ::fcntl(sock, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    ::inet_pton(AF_INET, "36.155.132.76", &addr.sin_addr.s_addr);

    int ret = connect(sock, (const sockaddr*)&addr, sizeof(addr));
	if (ret == -1) {
		if (errno == EINPROGRESS) {
			SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "add write event" << std::endl;
			scheduler->UpdateEvent(sock, EPOLLOUT, [scheduler]() {
				SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "writeable callback" << std::endl;
				timer_id = scheduler->RunAfter(std::chrono::milliseconds(500), [scheduler]() {
					static int count = 5;
					if (count) {
						SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "timer cb, count=" << count-- << std::endl;
					}

					if (count == 1) {
						scheduler->CancelTimer(timer_id);
					}
				}, true);
				close(sock);
			});
		}
	}
}

int main() {
	cc::Scheduler scheduler(3, false, "Test_Scheduler");
	scheduler.Start();
	DoConnect(&scheduler);
	sleep(3);
	scheduler.Stop();
}
