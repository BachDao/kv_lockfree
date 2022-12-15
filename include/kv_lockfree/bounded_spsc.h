//
// Created by Bach Dao.
//

#ifndef KV_LOCKFREE_BOUNDED_SPSC_H
#define KV_LOCKFREE_BOUNDED_SPSC_H
#include "utils.hpp"
#include <atomic>
#include <optional>
#include <type_traits>

/* Implementation of single-producer single-consumer bounded-size lockfree queue
 *
 * Both reader and writer use cache variable to reduce load from share variable
 * Reader:
 *  - Read index is cached and update on every read success
 *  - Write index is cached and only update when it's seem no more item
 *    to consume
 * Similar for writer, writerCachedReadIdx_ only update when it's seem out
 * of storage
 *
 * */

namespace kv_lockfree {
template <typename T> class bounded_spsc {
  size_t sizeMask_;
  alignas(KV_CACHE_LINE_SIZE) std::atomic<size_t> readIdx_{0};
  size_t readerCachedWriteIdx_ = 0;
  size_t readerCachedReadIdx_ = 0;
  alignas(KV_CACHE_LINE_SIZE) std::atomic<size_t> writeIdx_{0};
  size_t writerCachedReadIdx_ = 0;
  size_t writerCachedWriteIdx_ = 0;
  alignas(KV_CACHE_LINE_SIZE) std::unique_ptr<T[]> storage_;

public:
  explicit bounded_spsc(size_t queueSize);
  template <typename U>
    requires std::is_same_v<T, std::remove_cvref_t<U>>
  bool enqueue(U &&val);
  bool dequeue(T &outVal);
  std::optional<T> dequeue();
};
template <typename T> std::optional<T> bounded_spsc<T>::dequeue() {
  assert(std::is_default_constructible_v<T>);
  T outRet;
  if (dequeue(outRet)) {
    return {std::move(outRet)};
  }
  return std::nullopt;
}
template <typename T> bool bounded_spsc<T>::dequeue(T &outVal) {
  if (readerCachedReadIdx_ != readerCachedWriteIdx_ ||
      readerCachedReadIdx_ !=
          (readerCachedWriteIdx_ = writeIdx_.load(std::memory_order_acquire))) {
  }
  outVal = std::move(storage_[readerCachedReadIdx_ & sizeMask_]);
  readerCachedReadIdx_++;
  readIdx_.store(readerCachedReadIdx_, std::memory_order_release);
  return true;
}
template <typename T>
template <typename U>
  requires std::is_same_v<T, std::remove_cvref_t<U>>
bool bounded_spsc<T>::enqueue(U &&val) {
  if (writerCachedWriteIdx_ - writerCachedReadIdx_ > sizeMask_) {
    // update writer's cache
    writerCachedReadIdx_ = readIdx_.load(std::memory_order_relaxed);
    if (writerCachedWriteIdx_ - writerCachedReadIdx_ > sizeMask_) {
      return false;
    }
  }
  new (storage_.get() + (writerCachedWriteIdx_ & sizeMask_))
      T(std::forward<U>(val));
  writerCachedWriteIdx_++;
  writeIdx_.store(writerCachedWriteIdx_, std::memory_order_release);
  return true;
}
template <typename T>
bounded_spsc<T>::bounded_spsc(size_t queueSize)
    : storage_(std::make_unique<T[]>(queueSize)), sizeMask_(queueSize - 1) {
  assert((queueSize & sizeMask_) == 0 && "size must be power of 2");
}

} // namespace kv_lockfree
#endif // KV_LOCKFREE_BOUNDED_SPSC_H
