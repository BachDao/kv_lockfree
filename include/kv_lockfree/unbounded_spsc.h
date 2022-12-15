//
// Created by Bach Dao.
//
#include "utils.hpp"
#include <optional>
#include <type_traits>
#include <memory>
#include <atomic>
#include <cassert>
#ifndef KV_LOCKFREE_UNBOUNDED_SPSC_H
namespace kv_lockfree {
namespace detail {
template <typename T> struct spsc_segment {
  std::unique_ptr<T[]> storage_;
  std::atomic<spsc_segment *> nextSeg_ = nullptr;
  alignas(KV_CACHE_LINE_SIZE) std::atomic<uint64_t> readIdx_ = 0;
  uint64_t readerCachedReadIdx_ = 0;
  uint64_t readerCachedWriteIdx_ = 0;
  uint64_t readerCachedEpoch_ = 0;
  alignas(KV_CACHE_LINE_SIZE) std::atomic<uint64_t> writeIdx_ = 0;
  uint64_t writerCachedWriteIdx_ = 0;
  uint64_t writerCachedReadIdx_ = 0;
  std::atomic<uint64_t> epochVal_ = 0;

  spsc_segment(size_t segSize) : storage_(std::make_unique<T[]>(segSize)) {}
};

} // namespace detail
template <typename T> class unbounded_spsc {
  using segment_t = detail::spsc_segment<T>;
  const size_t sizeMask_;

  alignas(KV_CACHE_LINE_SIZE) std::atomic<segment_t *> readSeg_;
  alignas(KV_CACHE_LINE_SIZE) std::atomic<segment_t *> writeSeg_;

  KV_FORCE_INLINE bool has_empty_slot(uint64_t writeIdx, uint64_t readIdx);
  KV_FORCE_INLINE bool has_element(uint64_t readIdx, uint64_t writeIdx);
  KV_FORCE_INLINE bool read_from_segment(segment_t *ptrSeg, T &outVal);
  template <typename U>
  KV_FORCE_INLINE bool write_to_segment(segment_t *ptrSeg, U &&val);

public:
  explicit unbounded_spsc(size_t defaultSize);
  template <typename U>
    requires std::is_same_v<T, std::remove_cvref_t<U>>
  bool enqueue(U &&val);

  std::optional<T> dequeue();
  bool dequeue(T &outVal);
  segment_t *make_new_segment();
};
template <typename T>
template <typename U>
bool unbounded_spsc<T>::write_to_segment(unbounded_spsc::segment_t *ptrSeg,
                                         U &&val) {
  auto writeIdx = ptrSeg->writerCachedWriteIdx_;
  auto readIdx = ptrSeg->writerCachedReadIdx_;

  if (!has_empty_slot(writeIdx, readIdx)) { // update cache value and try again
    readIdx = ptrSeg->writerCachedReadIdx_ =
        ptrSeg->readIdx_.load(std::memory_order_relaxed);
  }

  if (has_empty_slot(writeIdx, readIdx)) {
    new (ptrSeg->storage_.get() + (writeIdx & sizeMask_))
        T(std::forward<U>(val));
    ptrSeg->writerCachedWriteIdx_ = writeIdx + 1;
    ptrSeg->writeIdx_.store(writeIdx + 1, std::memory_order_release);
    return true;
  }
  return false;
}
template <typename T>
bool unbounded_spsc<T>::read_from_segment(unbounded_spsc::segment_t *curSeg,
                                          T &outVal) {
  auto readIdx = curSeg->readerCachedReadIdx_;
  auto writeIdx = curSeg->readerCachedWriteIdx_;

  if (!has_element(readIdx, writeIdx)) { // update cache value and try again
    writeIdx = curSeg->readerCachedWriteIdx_ =
        curSeg->writeIdx_.load(std::memory_order_acquire);
  }

  if (has_element(readIdx, writeIdx)) {
    outVal = std::move(curSeg->storage_[readIdx & sizeMask_]);
    curSeg->readerCachedReadIdx_ = readIdx + 1;
    curSeg->readIdx_.store(readIdx + 1, std::memory_order_release);
    return true;
  }
  return false;
}
template <typename T>
bool unbounded_spsc<T>::has_element(uint64_t readIdx, uint64_t writeIdx) {
  return readIdx < writeIdx;
}

template <typename T> bool unbounded_spsc<T>::dequeue(T &outVal) {
  auto curSeg = readSeg_.load(std::memory_order_relaxed);
  if (read_from_segment(curSeg, outVal)) {
    return true;
  }
  /* It's seem that current segment is free, check the epoch value of current
   * segment to make sure it is truly free or not
   */
  auto epochVal = curSeg->epochVal_.load(std::memory_order_acquire);
  if (epochVal !=
      curSeg->readerCachedEpoch_) { // writer already seek to next segment
    // at this point, reader has memory view as late as writer at moment writer
    // seek to new segment, so we check current segment again to make sure it's
    // truly empty
    assert(curSeg->readerCachedEpoch_ + 1 == epochVal);
    if (read_from_segment(curSeg, outVal)) {
      // there are element in current segment
      // because reader doesn't seek to new segment yet, so we don't update cached
      // value of epoch
      return true;
    }

    // current segment is truly free, move to next segment and update epoch
    // value
    auto nexSeg = curSeg->nextSeg_.load(std::memory_order_relaxed);
    if (read_from_segment(nexSeg, outVal)) {
      curSeg->readerCachedEpoch_ = epochVal;
      readSeg_.store(nexSeg, std::memory_order_relaxed);
      return true;
    }
  }
  return false;
}
template <typename T>
typename unbounded_spsc<T>::segment_t *unbounded_spsc<T>::make_new_segment() {
  auto ptrSeg = new segment_t(sizeMask_ + 1);
  assert(ptrSeg);
  return ptrSeg;
}
template <typename T>
bool unbounded_spsc<T>::has_empty_slot(uint64_t writeIdx, uint64_t readIdx) {
  return writeIdx == readIdx || ((writeIdx - readIdx) & sizeMask_) != 0;
}
template <typename T>
unbounded_spsc<T>::unbounded_spsc(size_t defaultSize)
    : sizeMask_(defaultSize - 1) {
  assert((sizeMask_ & defaultSize) == 0);
  auto initialSeg = make_new_segment();
  initialSeg->nextSeg_ = initialSeg;
  readSeg_ = writeSeg_ = initialSeg;
}
template <typename T>
template <typename U>
  requires std::is_same_v<T, std::remove_cvref_t<U>>
bool unbounded_spsc<T>::enqueue(U &&val) {
  auto writeSeg = writeSeg_.load(std::memory_order_relaxed);
  if (write_to_segment(writeSeg, std::forward<U>(val))) {
    return true;
  }

  /* It's seem that no more free slot in current segment.
   * - If next segment isn't consuming by Reader, write new element to it
   * - Otherwise, we allocate new segment and do write.
   *
   * Everytime Writer seek to next segment (newly allocated or already exist),
   * it updates epoch value of current segment to make sure Reader have
   * consistent view of memory. It helps avoid situation where Reader see old
   * value of Writer's write index and come to wrong conclusion that segment is
   * empty
   *
   */
  auto nextSeg = writeSeg->nextSeg_.load(std::memory_order_relaxed);
  auto readSeg = readSeg_.load(std::memory_order_relaxed);

  if (nextSeg != readSeg) {
    write_to_segment(nextSeg, std::forward<U>(val));
    writeSeg_.store(nextSeg, std::memory_order_relaxed);
    writeSeg->epochVal_.fetch_add(1, std::memory_order_release);
    return true;
  }

  auto newSeg = make_new_segment();
  write_to_segment(newSeg, std::forward<U>(val));

  newSeg->nextSeg_.store(nextSeg, std::memory_order_relaxed);
  writeSeg->nextSeg_.store(newSeg, std::memory_order_relaxed);
  writeSeg_.store(newSeg, std::memory_order_relaxed);
  writeSeg->epochVal_.fetch_add(1, std::memory_order_release);
  return true;
}
} // namespace kv_lockfree
#define KV_LOCKFREE_UNBOUNDED_SPSC_H

#endif // KV_LOCKFREE_UNBOUNDED_SPSC_H
