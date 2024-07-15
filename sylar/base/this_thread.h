#pragma once

#include <sys/types.h>
#include <pthread.h>

#define INVALID_PTHREAD_ID 0
#define INVALID_TID -1

namespace sylar {
namespace base {

#define INVALID_PTHREAD_ID 0
#define INVALID_TID -1

::pid_t GetTid();

::pthread_t GetPthreadId();

} // namespace base
} // namespace sylar
