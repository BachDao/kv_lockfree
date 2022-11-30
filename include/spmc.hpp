#ifndef KV_LOCKFREE_SPMC_HPP
#define KV_LOCKFREE_SPMC_HPP
#include "segment.hpp"
#include <atomic>
#include <optional>

namespace kv_lockfree {
template <typename T>
  requires std::is_copy_constructible_v<T> || std::is_move_constructible_v<T>
class spmc_queue {
  std::atomic<segment<T> *> writeSeg_;
  segment<T> *cachedWriteSeg_ = nullptr;
  std::byte cacheLineFiller1[CACHE_LINE_SIZE -
                             sizeof(std::atomic<segment<T> *>) -
                             sizeof(segment<T> *)];
  std::atomic<segment<T> *> readSeg_;
  size_t readDeferCounter_ = 0;
  std::byte cachedLineFiller2[CACHE_LINE_SIZE -
                              sizeof(std::atomic<segment<T> *>) -
                              sizeof(segment<T> *) - sizeof(size_t)];
  const size_t readBatchSize_;
  const size_t sizeMask_;
  bool segment_has_free_slot(size_t writeIdx, size_t readIdx);
  bool write_to_new_allocate_segment(const T &val);
  bool try_enqueue_with_cached_index(const T &val);
  void update_writer_cached_index();
  bool probe_next_segment(const T &val);

public:
  explicit spmc_queue(size_t defaultSegmentSize, size_t readBatchSize = 1);
  bool enqueue(const T &val);
  bool enqueue(T &&val);
  bool dequeue(T &outVal);
  std::optional<T> dequeue();
};
template <typename T>
  requires std::is_copy_constructible_v<T> || std::is_move_constructible_v<T>
spmc_queue<T>::spmc_queue(size_t defaultSegmentSize, size_t readBatchSize)
    : readBatchSize_(readBatchSize), sizeMask_(defaultSegmentSize - 1) {
  assert((defaultSegmentSize & sizeMask_) == 0);
  auto ptrSeg = segment<T>::make_segment(defaultSegmentSize);
  ptrSeg->nextSeg_ = ptrSeg;
  readSeg_ = writeSeg_ = cachedWriteSeg_ = ptrSeg;
}
template <typename T>
  requires std::is_copy_constructible_v<T> ||
           std::is_move_constructible_v<T> bool
spmc_queue<T>::enqueue(const T &val) {
  if (try_enqueue_with_cached_index(val))
    return true;
  update_writer_cached_index();

  if (try_enqueue_with_cached_index(val))
    return true;
  if (probe_next_segment(val))
    return true;
  return write_to_new_allocate_segment(val);
}
template <typename T>
  requires std::is_copy_constructible_v<T> ||
           std::is_move_constructible_v<T> bool
spmc_queue<T>::try_enqueue_with_cached_index(const T &val) {
  auto curWriteSeg = cachedWriteSeg_;
  if (!segment_has_free_slot(cachedWriteSeg_->writerCachedWriteIdx_,
                             cachedWriteSeg_->writerCachedReadIdx_))
    return false;

  new (curWriteSeg->data_ + (curWriteSeg->writerCachedWriteIdx_ & sizeMask_))
      T(val);
  curWriteSeg->writerCachedWriteIdx_++;
  curWriteSeg->writeIdx_.store(curWriteSeg->writerCachedWriteIdx_,
                               std::memory_order_release);
  return true;
}
template <typename T>
  requires std::is_copy_constructible_v<T> || std::is_move_constructible_v<T>
void spmc_queue<T>::update_writer_cached_index() {
  auto curWriteSeg = cachedWriteSeg_;
  // we have only one writer, therefore writerCachedWriteIdx_ always up to date
  curWriteSeg->writerCachedReadIdx_ =
      curWriteSeg->readIdx_.load(std::memory_order_relaxed);
}
template <typename T>
  requires std::is_copy_constructible_v<T> ||
           std::is_move_constructible_v<T> bool
spmc_queue<T>::probe_next_segment(const T &val) {
  auto nextSeg = cachedWriteSeg_->nextSeg_.load(std::memory_order_relaxed);
  if (nextSeg == readSeg_.load(std::memory_order_relaxed))
    return false;
  new (nextSeg->data_ + nextSeg->writerCachedWriteIdx_) T(val);
  nextSeg->writerCachedWriteIdx_++;
  nextSeg->writeIdx_.store(nextSeg->writerCachedWriteIdx_,
                           std::memory_order_relaxed);
  cachedWriteSeg_ = nextSeg;
  writeSeg_.store(nextSeg, std::memory_order_relaxed);
}
template <typename T>
  requires std::is_copy_constructible_v<T> ||
           std::is_move_constructible_v<T> bool
spmc_queue<T>::write_to_new_allocate_segment(const T &val) {
  auto newSeg = segment<T>::make_segment(sizeMask_ + 1);
  new (newSeg->data_) T(val);
  newSeg->writerCachedWriteIdx_ = 1;
  newSeg->writeIdx_.store(newSeg->writerCachedWriteIdx_,
                          std::memory_order_relaxed);
  newSeg->nextSeg_.store(cachedWriteSeg_->nextSeg_, std::memory_order_relaxed);
  cachedWriteSeg_->nextSeg_.store(newSeg, std::memory_order_relaxed);
  cachedWriteSeg_ = newSeg;
  writeSeg_.store(newSeg, std::memory_order_release);
  return true;
}
template <typename T>
  requires std::is_copy_constructible_v<T> ||
           std::is_move_constructible_v<T> bool
spmc_queue<T>::dequeue(T &outVal) {
  auto curSeg = readSeg_.load(std::memory_order_relaxed);
  auto readIdx = curSeg->readIdx_.load(std::memory_order_relaxed);
  while (!curSeg->readIdx_.compare_exchange_weak(readIdx, readIdx + 1,
                                                 std::memory_order_acquire,
                                                 std::memory_order_relaxed))
    ;
}

} // namespace kv_lockfree

#endif // KV_LOCKFREE_SPMC_HPP
