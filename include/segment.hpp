#ifndef KV_LOCKFREE_SEGMENT_HPP
#define KV_LOCKFREE_SEGMENT_HPP
#include "utils.hpp"
#include <atomic>
#include <memory>
namespace kv_lockfree {
template <typename T> struct segment {
  alignas(CACHE_LINE_SIZE) std::atomic<index_t> writeIdx_{0};
  index_t writerCachedReadIdx_ = 0;
  index_t writerCachedWriteIdx_ = 0;

  alignas(CACHE_LINE_SIZE) std::atomic<index_t> readIdx_{0};
  index_t readerCachedReadIdx_ = 0;
  index_t readerCachedWriteIdx_ = 0;
  index_t readDeferCounter_ = 0;

  std::atomic<segment *> nextSeg_ = nullptr;
  T *data_ = nullptr;
  std::byte *addr_ = nullptr;
  segment() {}
  ~segment() {
    if (addr_) {
      delete[] addr_;
    }
  }
  static segment *make_segment(size_t size) {
    auto requestSize = sizeof(segment<T>) + std::alignment_of_v<segment<T>> - 1;
    requestSize += sizeof(T) * size + std::alignment_of_v<T> - 1;
    void *ptrRawStorage = new std::byte[requestSize];
    auto ptrSegment =
        std::align(std::alignment_of_v<segment<T>>, sizeof(segment<T>),
                   ptrRawStorage, requestSize);
    assert(ptrSegment);
    void *tmpPtrData =
        reinterpret_cast<std::byte *>(ptrSegment) + sizeof(segment<T>);
    requestSize -= sizeof(segment<T>);
    auto ptrData = std::align(std::alignment_of_v<T>, sizeof(T) * size,
                              tmpPtrData, requestSize);
    assert(ptrData);
    auto newSeg = new (ptrSegment) segment<T>();
    newSeg->data_ = reinterpret_cast<T *>(ptrData);
    newSeg->addr_ = reinterpret_cast<std::byte *>(ptrRawStorage);
    return reinterpret_cast<segment<T> *>(ptrSegment);
  }
};
} // namespace kv_lockfree
#endif // KV_LOCKFREE_SEGMENT_HPP
