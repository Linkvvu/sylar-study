#pragma once

#include <sys/types.h>
#include <pthread.h>

namespace sylar {
namespace base {

::pid_t GetTid();

::pthread_t GetPthreadId();

} // namespace base
} // namespace sylar
