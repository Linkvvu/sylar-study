#include "this_thread.h"

#include <unistd.h>
#include <sys/syscall.h>

namespace {

thread_local static ::pid_t tl_tid = 0;
thread_local static ::pthread_t tl_pthread_id = 0;

static void CacheTid() {
    if (tl_tid == 0) {
        tl_tid = ::syscall(SYS_gettid);
    }
}

static void CachePthreadId() {
    if (tl_pthread_id == 0) {
        tl_pthread_id = ::pthread_self();
    }
}

} // namespace

::pid_t sylar::base::GetTid() {
    if (__builtin_expect(tl_tid == 0, 0)) {
        CacheTid();
    }
    return tl_tid;
}

::pthread_t sylar::base::GetPthreadId() {
    if (__builtin_expect(tl_pthread_id == 0, 0)) {
        CachePthreadId();
    }
    return tl_pthread_id;
}
