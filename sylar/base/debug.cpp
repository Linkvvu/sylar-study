#include "debug.h"

#include <vector>
#include <sstream>
#include <execinfo.h>

namespace {

static std::vector<std::string> Backtrace(size_t trunc) {
    // the coroutine has the tiny stack scope
    // so let's allocate from heap
#define BUFFER_CAPACITY 64
    void** buffer = (void**)::malloc(sizeof(void*) * BUFFER_CAPACITY);    
    size_t nptrs = static_cast<size_t>(backtrace(buffer, BUFFER_CAPACITY));
    char** strings = backtrace_symbols(buffer, nptrs);
    if (strings == nullptr) {
        SYLAR_LOG_ERROR(SYLAR_SYS_LOGGER()) << "failed to call ::backtrace_symbols";
        return {};
    }

    // logic check
    if (__builtin_expect(nptrs < trunc, 0)) {
        return {};
    }

    std::vector<std::string> result(nptrs - trunc);
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = strings[i + trunc];
    }

    ::free(strings);
    ::free(buffer);

    return result;
}

} // namespace 

std::string sylar::base::BacktraceToString(size_t trunc, const char* prefix) {
    const auto& symbols = Backtrace(trunc);
    std::ostringstream oss;
    for (size_t i = 0; i < symbols.size(); ++i) {
        oss << prefix << symbols[i] << std::endl;
    }
    return oss.str();
}