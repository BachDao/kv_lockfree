//
// Created by Bach Dao.
//

#ifndef KV_LOCKFREE_BOUNDED_SPMC_H
#define KV_LOCKFREE_BOUNDED_SPMC_H
#include "utils.hpp"
#include <memory>
#include <optional>
#include <type_traits>
namespace kv_lockfree {
template <typename T>
  requires std::is_default_constructible_v<T>
class bounded_spmc {
  struct node {
    T element_;
    std::atomic<uint64_t> seqNo_;
  };
  using node_t = node;
  std::unique_ptr<node_t[]> storage_;
  const size_t sizeMask_;

  // writeIdx_ is access only by producer, don't need to be atomic
  alignas(KV_CACHE_LINE_SIZE) uint64_t writeIdx_ = 0;
  alignas(KV_CACHE_LINE_SIZE) std::atomic<uint64_t> readIdx_ = 0;

public:
  explicit bounded_spmc(size_t queueSize)
      : storage_{std::make_unique<node_t[]>(queueSize)},
        sizeMask_(queueSize - 1) {
    assert((queueSize & sizeMask_) == 0);
    for (int i = 0; i < queueSize; ++i) {
      storage_[i].seqNo_ = i;
    }
  }

  template <typename U> bool enqueue(U &&val) {
    auto &writeNode = storage_[writeIdx_ & sizeMask_];
    uint64_t seqNo = writeNode.seqNo_.load(std::memory_order_acquire);
    auto diff = seqNo - writeIdx_;
    if (diff == 0) {
      writeIdx_++;
      writeNode.element_ = std::forward<U>(val);
      writeNode.seqNo_.store(writeIdx_, std::memory_order_release);
      return true;
    }
    return false;
  }

  bool dequeue(T &outVal) {
    auto readPos = readIdx_.load(std::memory_order_relaxed);
    node_t *ptrNode = nullptr;
    while (true) {
      ptrNode = &storage_[readPos & sizeMask_];
      auto diff = ptrNode->seqNo_.load(std::memory_order_acquire) - readPos;
      if (diff == 1) { // slot has data
        if (readIdx_.compare_exchange_weak(readPos, readPos + 1,
                                           std::memory_order_relaxed))
          break;
      } else if (diff < 1) { // slot is empty
        return false;
      } else { // diff > 1 : someone else come before us, retry again
        readPos = readIdx_.load(std::memory_order_relaxed);
      }
    }
    outVal = std::move(ptrNode->element_);
    ptrNode->seqNo_.store(readPos + sizeMask_ + 1, std::memory_order_release);
    return true;
  }
  std::optional<T> dequeue();
};
} // namespace kv_lockfree
#endif // KV_LOCKFREE_BOUNDED_SPMC_H
