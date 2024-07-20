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
			scheduler->UpdateEvent(sock, EPOLLOUT, []() {
				SYLAR_LOG_INFO(SYLAR_ROOT_LOGGER()) << "writeable callback" << std::endl;
				close(sock);
			});
		}
	}
}

int main() {
	cc::Scheduler scheduler(3, false, "Test_Scheduler");
	scheduler.Start();
	DoConnect(&scheduler);
	sleep(1);
	scheduler.Stop();
}
