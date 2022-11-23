#include <cstddef>
#include <new>
namespace kv_lockfree {
namespace detail {
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_CPU_ARM64
#define CACHE_LINE_SIZE 128
#else
#define  CACHE_LINE_SIZE 64
#endif
#else
#define CACHE_LINE_SIZE 64
#endif
} // namespace detail

using index_t = unsigned long long;
} // namespace kv_lockfree