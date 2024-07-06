#pragma once

#include "log.h"

#include <string>
#include <cassert>

namespace sylar {
namespace base {

    std::string BacktraceToString(size_t trunc = 2, const char* prefix = "");

} // namespace base
} // namespace sylar

#define SYLAR_ASSERT(expr)  \
    if (!(expr)) {  \
        SYLAR_LOG_FATAL(SYLAR_ROOT_LOGGER())    \
            << "ASSERT: " #expr << std::endl    \
            << "backtrace:" << std::endl        \
            << sylar::base::BacktraceToString(2, "\t"); \
        assert((expr)); \
    }

#define SYLAR_ASSERT_WITH_MSG(expr, msg)    \
    if (!(expr)) {  \
        SYLAR_LOG_FATAL(SYLAR_ROOT_LOGGER())    \
            << "ASSERT: " #expr << std::endl    \
            << #msg << std::endl                \
            << "backtrace:" << std::endl        \
            << sylar::base::BacktraceToString(2, "\t"); \
        assert((expr)); \
    }
