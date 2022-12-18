//
// Created by Bach Dao.
//

#ifndef KV_LOCKFREE_UNBOUNDED_SPMC_H
#define KV_LOCKFREE_UNBOUNDED_SPMC_H
#include "utils.hpp"
#include <atomic>
#include <memory>
#include <optional>
#include <type_traits>
template <typename T> class unbounded_spmc {
  struct node_t {
    std::atomic<uint64_t> seqNo_;
    T val_;
  };
  struct segment_t {
    alignas(KV_CACHE_LINE_SIZE) std::atomic<uint64_t> readIdx_ = 0;
    alignas(KV_CACHE_LINE_SIZE) std::atomic<uint64_t> writeIdx_ = 0;

    const std::unique_ptr<node_t[]> storage_;
    std::atomic<segment_t *> nextSeg_ = nullptr;
    segment_t(size_t size) : storage_(std::make_unique<node_t[]>(size)) {
      for (int i = 0; i < size; ++i) {
        storage_[i].seqNo_.store(i, std::memory_order_relaxed);
      }
      std::atomic_thread_fence(std::memory_order_release);
    }
    node_t &node_at(size_t idx) { return storage_[idx]; }
  };
  segment_t *make_segment(size_t size) {
    auto ptrSeg = new segment_t(size);
    assert(ptrSeg);
    return ptrSeg;
  }
  alignas(KV_CACHE_LINE_SIZE) std::atomic<segment_t *> readSeg_ = nullptr;
  alignas(KV_CACHE_LINE_SIZE) std::atomic<segment_t *> writeSeg_ = nullptr;
  const size_t sizeMask_;

  template <typename U>
  KV_FORCE_INLINE bool write_to_segment(segment_t *ptrSeg, U &&val) {
    auto writeIdx = ptrSeg->writeIdx_.load(std::memory_order_relaxed);
    auto pos = writeIdx & sizeMask_;
    auto &node = ptrSeg->storage_[pos];
    auto diff = node.seqNo_.load(std::memory_order_acquire) - pos;

    if (diff == 0) {
      ptrSeg->writeIdx_.store(writeIdx + 1, std::memory_order_relaxed);
      node.val_ = std::forward<U>(val);
      node.seqNo_.store(pos + 1, std::memory_order_release);
      return true;
    }
    return false;
  }
  KV_FORCE_INLINE bool read_from_segment(segment_t *ptrSeg, T &outVal) {
    auto readIdx = ptrSeg->readIdx_.load(std::memory_order_relaxed);
    size_t pos;
    node_t *ptrNode;
    while (true) {
      pos = readIdx & sizeMask_;
      ptrNode = &ptrSeg->node_at(pos);
      auto diff = ptrNode->seqNo_.load(std::memory_order_acquire) - readIdx;
      if (diff > 0) {
        if (ptrSeg->readIdx_.compare_exchange_weak(readIdx, readIdx + 1,
                                                   std::memory_order_relaxed))
          break;
      } else {
        return false;
      }
    }
    outVal = std::move(ptrNode->val_);
    ptrNode->seqNo_.store(readIdx + sizeMask_ + 1, std::memory_order_release);
    return true;
  }

public:
  unbounded_spmc(size_t defaultSize) : sizeMask_(defaultSize - 1) {
    auto ptrSeg = make_segment(defaultSize);
    ptrSeg->nextSeg_.store(ptrSeg, std::memory_order_relaxed);
    readSeg_ = writeSeg_ = ptrSeg;
  }
  // High-level:
  // - If current segment has free slot, put new item to it
  // - Else If next segment isn't consuming by readers and not full yet,
  //   put new item there
  // - Otherwise, allocate new segment and write new item to it

  template <typename U> bool enqueue(U &&val) {
    auto writeSeg = writeSeg_.load(std::memory_order_relaxed);
    if (write_to_segment(writeSeg, std::forward<U>(val))) {
      return true;
    }
    auto nextSeg = writeSeg->nextSeg_.load(std::memory_order_relaxed);
    if (nextSeg != readSeg_.load(std::memory_order_relaxed) &&
        write_to_segment(nextSeg, std::forward<U>(val))) {

      writeSeg_.store(nextSeg, std::memory_order_release);
      return true;
    }
    auto ptrSeg = make_segment(sizeMask_ + 1);
    // write data to first node
    assert(write_to_segment(ptrSeg, std::forward<U>(val)));

    // update next segment to newly allocated
    ptrSeg->nextSeg_.store(writeSeg->nextSeg_.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
    writeSeg->nextSeg_.store(ptrSeg, std::memory_order_relaxed);
    writeSeg_.store(ptrSeg, std::memory_order_release);
    return true;
  }
  bool dequeue(T &outVal) {
    auto readSeg = readSeg_.load(std::memory_order_relaxed);

    if (read_from_segment(readSeg, outVal)) {
      return true;
    }
    auto writeSeg = writeSeg_.load(std::memory_order_acquire);
    if (readSeg != writeSeg) {
      if (read_from_segment(readSeg, outVal))
        return true;
      if (readSeg_.compare_exchange_weak(readSeg, readSeg->nextSeg_,
                                         std::memory_order_relaxed))
        ;
      return dequeue(outVal);
    }
    return false;
  }
  std::optional<T> dequeue();
};
#endif // KV_LOCKFREE_UNBOUNDED_SPMC_H
