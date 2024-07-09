#include <concurrency/thread.h>
#include <base/this_thread.h>
#include <base/log.h>
#include <mutex>

using namespace sylar;
namespace cc = concurrency;

auto sylar_logger = SYLAR_ROOT_LOGGER();

int count = 0;
std::mutex g_mutex;

void fun1() {
    SYLAR_LOG_INFO(sylar_logger) /*<< "name: " << base::GetThreadName()*/
			<< " this.name: " << cc::Thread::GetThisThread()->GetName()
			<< " id: " << base::GetTid()
			<< " this.id: " << cc::Thread::GetThisThread()->GetTid()
			<< std::endl;

    for(size_t i = 0; i < 100000; ++i) {
		std::lock_guard<std::mutex> guard(g_mutex);
        ++count;
    }
}

int main(int argc, char** argv) {
    SYLAR_LOG_INFO(sylar_logger) << "thread test begin" << std::endl;

    std::vector<std::unique_ptr<cc::Thread>> pool(3);
    for(size_t i = 0; i < 3; ++i) {
        pool[i].reset(new cc::Thread(&fun1, "Thread-" + std::to_string(i)));
    }

    for(size_t i = 0; i < pool.size(); ++i) {
        pool[i]->Join();
    }

    SYLAR_LOG_INFO(sylar_logger) << "thread test end" << std::endl;
    SYLAR_LOG_INFO(sylar_logger) << "count=" << count << std::endl;

    return 0;
}
