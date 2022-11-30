#ifndef KV_LOCKFREE_SPSC_HPP
#define KV_LOCKFREE_SPSC_HPP

#include "segment.hpp"
#include "utils.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>
namespace kv_lockfree {
template <typename T> void log(T v) { std::cout << v << "\n"; }
template <typename T, typename... Ts> void log(T v, Ts... vs) {
  std::cout << v;
  log(vs...);
}

template <typename T> class alignas(CACHE_LINE_SIZE) bounded_spsc_queue {
  std::atomic<index_t> writeIdx_{0};
  index_t cachedReadIdx_ = 0;
  std::byte cacheLineFiller1[CACHE_LINE_SIZE - 2 * sizeof(index_t) -
                             sizeof(std::atomic<index_t>)];
  std::atomic<index_t> readIdx_{0};
  index_t readDeferCounter_ = 0;
  index_t cachedWriteIdx_ = 0;
  std::byte cacheLineFiller2[CACHE_LINE_SIZE - 2 * sizeof(index_t) -
                             sizeof(std::atomic<index_t>)];
  size_t sizeMask_;
  std::unique_ptr<T[]> data_ = nullptr;
  const unsigned short batchSize_;
  template <typename U>
    requires std::is_convertible_v<U, T> bool
  push_impl(U &&val);

public:
  explicit bounded_spsc_queue(size_t bufferSize, size_t batchReadSize = 1);
  std::optional<T> pop();
  bool pop(T &outVal);
  bool push(const T &val);
  bool push(T &&val);
};

template <typename T>
bounded_spsc_queue<T>::bounded_spsc_queue(size_t bufferSize,
                                          size_t batchReadSize)
    : sizeMask_(bufferSize - 1), batchSize_(batchReadSize) {
  assert((bufferSize & sizeMask_) == 0 && "size must be power of 2");
  auto ptr = new (std::align_val_t(sizeof(T))) T[sizeof(T) * (sizeMask_ + 1)];
  data_.reset(reinterpret_cast<T *>(ptr));
}
template <typename T> bool bounded_spsc_queue<T>::pop(T &outVal) {
  auto localHead = writeIdx_.load(std::memory_order_relaxed);
  auto localTail = readIdx_.load(std::memory_order_acquire);

  if (localHead <= localTail) {
    return false;
  }
  auto ptrResult = data_.get() + (localTail & sizeMask_);
  outVal = *ptrResult;
  ptrResult->~T();
  readIdx_.store(localTail + 1, std::memory_order_relaxed);
  return true;
}
template <typename T> bool bounded_spsc_queue<T>::push(const T &val) {
  return push_impl(val);
}
template <typename T> bool bounded_spsc_queue<T>::push(T &&val) {
  return push_impl((T &&) val);
}
template <typename T>
template <typename U>
  requires std::is_convertible_v<U, T> bool
bounded_spsc_queue<T>::push_impl(U &&val) {
  auto localHead = writeIdx_.load(std::memory_order_relaxed);
  if (localHead - cachedReadIdx_ > sizeMask_) { // maybe out of storage
    cachedReadIdx_ = readIdx_.load(std::memory_order_relaxed);
    if (localHead - cachedReadIdx_ > sizeMask_) { // out of storage
      return false;
    }
  }
  auto writeIdx = localHead & sizeMask_;
  auto ptrStorage = data_.get() + writeIdx;
  new (ptrStorage) T(std::forward<U>(val));
  writeIdx_.store(localHead + 1, std::memory_order_release);
  return true;
}
template <typename T> std::optional<T> bounded_spsc_queue<T>::pop() {
  auto localTail = readIdx_.load(std::memory_order_acquire);
  if (localTail >= cachedWriteIdx_) { // maybe empty
    cachedWriteIdx_ = writeIdx_.load(std::memory_order_relaxed);
    if (localTail >= cachedWriteIdx_) { // empty
      return std::nullopt;
    }
  }
  auto readIdx = (localTail + readDeferCounter_) & sizeMask_;
  auto ptrResult = data_.get() + readIdx;
  auto retVal = *ptrResult;
  ptrResult->~T();
  if (readDeferCounter_ == batchSize_) {
    readIdx_.store(localTail + batchSize_, std::memory_order_relaxed);
    readDeferCounter_ = 1;
  } else {
    readDeferCounter_++;
  }
  return {retVal};
}
template <typename T> class spsc_queue {
  std::atomic<segment<T> *> writeSeg_;
  segment<T> *cachedWriteSeg_ = nullptr;
  std::byte cacheLineFiller1[CACHE_LINE_SIZE -
                             sizeof(std::atomic<segment<T> *>) -
                             sizeof(segment<T> *)];
  segment<T> *cachedReadSeg_ = nullptr;
  std::atomic<segment<T> *> readSeg_;
  size_t readDeferCounter_ = 0;
  std::byte cachedLineFiller2[CACHE_LINE_SIZE -
                              sizeof(std::atomic<segment<T> *>) -
                              sizeof(segment<T> *) - sizeof(size_t)];
  const size_t readBatchSize_;
  const size_t defaultSize_;
  const size_t sizeMask_;
  bool segment_has_free_slot(index_t writeIdx, index_t readIdx);
  bool segment_has_data(index_t writeIdx, index_t readIdx);
  KV_FORCE_INLINE bool try_dequeue_with_cached_index(T &outVal,
                                                     segment<T> *ptrSegment);
  KV_FORCE_INLINE void update_segment_cached_index(segment<T> *ptrSeg);
  KV_FORCE_INLINE bool is_last_segment();
  KV_FORCE_INLINE bool probe_next_segment(T &outResult);
  bool dequeue_impl(T &outResult);

public:
  explicit spsc_queue(size_t defaultSize, size_t readBatchSize = 1);
  ~spsc_queue();
  bool enqueue(const T &val);
  bool enqueue(T &&val);
  bool dequeue(T &outVal);
  std::optional<T> dequeue();
};
template <typename T>
spsc_queue<T>::spsc_queue(size_t defaultSize, size_t readBatchSize)
    : readBatchSize_(readBatchSize), defaultSize_(defaultSize),
      sizeMask_(defaultSize - 1) {
  assert((defaultSize_ & sizeMask_) == 0);
  auto ptrSeg = segment<T>::make_segment(defaultSize);
  ptrSeg->nextSeg_ = ptrSeg;
  cachedWriteSeg_ = ptrSeg;
  cachedReadSeg_ = ptrSeg;
  writeSeg_.store(ptrSeg, std::memory_order_relaxed);
  readSeg_.store(ptrSeg, std::memory_order_seq_cst);
}
template <typename T> bool spsc_queue<T>::enqueue(const T &val) {
  auto curSeg = cachedWriteSeg_;
  if (segment_has_free_slot(curSeg->writerCachedWriteIdx_,
                            curSeg->writerCachedReadIdx_)) {
    new (curSeg->data_ + (curSeg->writerCachedWriteIdx_ & sizeMask_)) T(val);
    curSeg->writerCachedWriteIdx_++;
    curSeg->writeIdx_.store(curSeg->writerCachedWriteIdx_, std::memory_order_release);
    return true;
  } else {
    curSeg->writerCachedReadIdx_ =
        curSeg->readIdx_.load(std::memory_order_relaxed);
    if (segment_has_free_slot(curSeg->writerCachedWriteIdx_,
                              curSeg->writerCachedReadIdx_)) {
      new (curSeg->data_ + (curSeg->writerCachedWriteIdx_ & sizeMask_)) T(val);
      curSeg->writerCachedWriteIdx_++;
      curSeg->writeIdx_.store(curSeg->writerCachedWriteIdx_,
                              std::memory_order_release);
      return true;
    }
  }

  auto nextSeg = curSeg->nextSeg_.load(std::memory_order_relaxed);
  auto nextSegWriteIdx = nextSeg->writerCachedWriteIdx_;
  if (nextSeg != readSeg_.load(std::memory_order_relaxed)) {
    new (nextSeg->data_ + (nextSegWriteIdx & sizeMask_)) T(val);
    nextSeg->writerCachedWriteIdx_++;
    nextSeg->writeIdx_.store(nextSeg->writerCachedWriteIdx_,
                             std::memory_order_relaxed);
    cachedWriteSeg_ = nextSeg;
    writeSeg_.store(nextSeg, std::memory_order_release);
    return true;
  }

  auto newSeg = segment<T>::make_segment(defaultSize_);
  new (newSeg->data_) T(val);
  newSeg->writerCachedWriteIdx_ = 1;
  newSeg->writeIdx_.store(newSeg->writerCachedWriteIdx_, std::memory_order_relaxed);
  newSeg->nextSeg_.store(nextSeg, std::memory_order_relaxed);
  curSeg->nextSeg_.store(newSeg, std::memory_order_relaxed);
  cachedWriteSeg_ = newSeg;
  writeSeg_.store(newSeg, std::memory_order_release);
  return true;
}

template <typename T> std::optional<T> spsc_queue<T>::dequeue() {
  T outVal;
  if (dequeue_impl(outVal)) {
    return {outVal};
  }
  return std::nullopt;
}
template <typename T> bool spsc_queue<T>::probe_next_segment(T &outResult) {
  auto nextSeg = this->cachedReadSeg_->nextSeg_.load(std::memory_order_acquire);
  update_segment_cached_index(nextSeg);
  auto success = try_dequeue_with_cached_index(outResult, nextSeg);
  if (success) {
    this->cachedReadSeg_ = nextSeg;
    this->readSeg_.store(this->cachedReadSeg_, std::memory_order_relaxed);
    return true;
  }
  return false;
}
template <typename T> bool spsc_queue<T>::is_last_segment() {
  return cachedReadSeg_ == writeSeg_.load(std::memory_order_relaxed);
}
template <typename T>
bool spsc_queue<T>::segment_has_free_slot(index_t writeIdx, index_t readIdx) {
  return ((writeIdx - readIdx) & defaultSize_) == 0;
}
template <typename T>
bool spsc_queue<T>::segment_has_data(index_t writeIdx, index_t readIdx) {
  return readIdx < writeIdx;
}
template <typename T> spsc_queue<T>::~spsc_queue() {
  auto writeSeg = writeSeg_.load(std::memory_order_seq_cst);
  auto readSeg = readSeg_.load(std::memory_order_relaxed);
  while (writeSeg != readSeg) {
    writeSeg->~segment<T>();
    writeSeg = writeSeg->nextSeg_;
  }
  readSeg->~segment<T>();
}
template <typename T>
bool spsc_queue<T>::try_dequeue_with_cached_index(T &outVal,
                                                  segment<T> *ptrSegment) {
  auto curSegReadIdx =
      ptrSegment->readerCachedReadIdx_ + ptrSegment->readDeferCounter_;
  auto curSegWriteIdx = ptrSegment->readerCachedWriteIdx_;
  if (segment_has_data(curSegWriteIdx, curSegReadIdx)) {
    outVal = ptrSegment->data_[curSegReadIdx & sizeMask_];
    ptrSegment->readDeferCounter_++;
    if (ptrSegment->readDeferCounter_ == readBatchSize_) {
      ptrSegment->readerCachedReadIdx_ += ptrSegment->readDeferCounter_;
      ptrSegment->readDeferCounter_ = 0;
      ptrSegment->readIdx_.store(ptrSegment->readerCachedReadIdx_,
                                 std::memory_order_relaxed);
    }
    return true;
  }
  return false;
}
template <typename T>
void spsc_queue<T>::update_segment_cached_index(segment<T> *ptrSeg) {
  ptrSeg->readerCachedWriteIdx_ =
      ptrSeg->writeIdx_.load(std::memory_order_relaxed);
}
template <typename T> bool spsc_queue<T>::dequeue_impl(T &outResult) {
  if (try_dequeue_with_cached_index(outResult, cachedReadSeg_)) {
    return true;
  }
  update_segment_cached_index(cachedReadSeg_);
  if (try_dequeue_with_cached_index(outResult, cachedReadSeg_)) {
    return true;
  }
  if (is_last_segment()) {
    return false;
  }
  return probe_next_segment(outResult);
}
template <typename T> bool spsc_queue<T>::dequeue(T &outVal) {
  return dequeue_impl(outVal);
}

} // namespace kv_lockfree
#endif // KV_LOCKFREE_SPMC_HPP
