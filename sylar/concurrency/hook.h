#pragma once

namespace sylar {
namespace concurrency {

using fcntl_libc_func_t = int(*)(int fd, int cmd, ...);
extern fcntl_libc_func_t fcntl_libc_func;

} // namespace concurrency
} // namespace sylar
