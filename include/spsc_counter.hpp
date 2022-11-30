#ifndef KV_LOCKFREE_SPSC_COUNTER_HPP
#define KV_LOCKFREE_SPSC_COUNTER_HPP
#include "utils.hpp"
#include <atomic>
namespace kv_lockfree {
template <typename T> class spsc_counter {
  struct cell_t {
    std::atomic<bool> empty_;
    T data_;
  };
  alignas(CACHE_LINE_SIZE) const size_t sizeMask_;
  cell_t *ptrCells_;
  alignas(CACHE_LINE_SIZE) index_t enqueuePos_ = 0;
  alignas(CACHE_LINE_SIZE) index_t dequeuePos_ = 0;

public:
  explicit spsc_counter(size_t defaultSize);
  bool enqueue(const T &val);
  bool dequeue(T &outVal);
};
template <typename T>
spsc_counter<T>::spsc_counter(size_t defaultSize) : sizeMask_(defaultSize - 1) {
  ptrCells_ = new cell_t[sizeMask_ + 1];
  for (int i = 0; i < defaultSize; ++i) {
    ptrCells_[i].empty_.store(true, std::memory_order_relaxed);
  }
  std::atomic_thread_fence(std::memory_order_seq_cst);
}
template <typename T> bool spsc_counter<T>::enqueue(const T &val) {
  auto& ptrCell = ptrCells_[enqueuePos_ & sizeMask_];
  auto isEmpty = ptrCell.empty_.load(std::memory_order_relaxed);
  if (isEmpty) {
    new (&ptrCell.data_) T(val);
    enqueuePos_++;
    ptrCell.empty_.store(false, std::memory_order_release);
    return true;
  }
  return false;
}
template <typename T> bool spsc_counter<T>::dequeue(T &outVal) {
  auto& ptrCell = ptrCells_[dequeuePos_ & sizeMask_];
  auto isEmpty = ptrCell.empty_.load(std::memory_order_acquire);
  if (!isEmpty) {
    outVal = ptrCell.data_;
    dequeuePos_++;
    ptrCell.empty_.store(true, std::memory_order_relaxed);
    return true;
  }
  return false;
}
} // namespace kv_lockfree
#endif // KV_LOCKFREE_SPSC_COUNTER_HPP
