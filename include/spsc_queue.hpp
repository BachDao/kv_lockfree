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

template <typename T> struct segment {
  static std::atomic<int> id_;
  int currentId_;
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
  std::atomic<segment *> nextSeg_ = nullptr;
  size_t size_;
  T *data_ = nullptr;
  std::byte *addr_ = nullptr;
  segment(size_t size) : size_(size) {}
  ~segment() {
    if (addr_) {
      delete[] addr_;
    }
  }
  static segment *make_segment(size_t size) {
    auto requestSize = sizeof(segment<T>) + std::alignment_of_v<segment<T>> - 1;
    requestSize += sizeof(T) * size + std::alignment_of_v<T> - 1;
    void *ptrRawStorage = new std::byte[requestSize];
    assert(ptrRawStorage);
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
    auto newSeg = new (ptrSegment) segment<T>(size);
    newSeg->data_ = reinterpret_cast<T *>(ptrData);
    newSeg->addr_ = reinterpret_cast<std::byte *>(ptrRawStorage);
    auto prev = id_.fetch_add(1, std::memory_order_relaxed);
    newSeg->currentId_ = prev;
    return reinterpret_cast<segment<T> *>(ptrSegment);
  }
};
template <typename T> std::atomic<int> segment<T>::id_{0};

template <typename T> class spsc_queue {
  std::atomic<segment<T> *> writeSeg_;
  std::atomic<segment<T> *> readSeg_;
  size_t defaultSize_;
  size_t sizeMask_;
  bool segment_has_free_slot(index_t writeIdx, index_t readIdx);
  bool segment_has_data(index_t writeIdx, index_t readIdx);

public:
  explicit spsc_queue(size_t defaultSize);
  ~spsc_queue();
  bool enqueue(const T &val);
  bool enqueue(T &&val);
  bool dequeue(T &outVal);
  std::optional<T> dequeue();
};
template <typename T>
spsc_queue<T>::spsc_queue(size_t defaultSize)
    : defaultSize_(defaultSize), sizeMask_(defaultSize - 1) {
  assert((defaultSize_ & sizeMask_) == 0);
  auto ptrSeg = segment<T>::make_segment(defaultSize);
  ptrSeg->nextSeg_ = ptrSeg;
  writeSeg_.store(ptrSeg, std::memory_order_relaxed);
  readSeg_.store(ptrSeg, std::memory_order_seq_cst);
}
template <typename T> bool spsc_queue<T>::enqueue(const T &val) {
  auto curSeg = writeSeg_.load(std::memory_order_relaxed);
  auto writeIdx = curSeg->writeIdx_.load(std::memory_order_relaxed);
  auto readIdx = curSeg->readIdx_.load(std::memory_order_relaxed);
  if (segment_has_free_slot(writeIdx, readIdx)) {
    new (curSeg->data_ + (writeIdx & sizeMask_)) T(val);
    curSeg->writeIdx_.store(writeIdx + 1, std::memory_order_release);
    return true;
  }

  auto nextSeg = curSeg->nextSeg_.load(std::memory_order_relaxed);
  auto nextSegWriteIdx = nextSeg->writeIdx_.load(std::memory_order_relaxed);
  if (nextSeg != readSeg_.load(std::memory_order_relaxed)) {
    new (nextSeg->data_ + (nextSegWriteIdx & sizeMask_)) T(val);
    nextSeg->writeIdx_.store(nextSegWriteIdx + 1, std::memory_order_relaxed);
    writeSeg_.store(nextSeg, std::memory_order_release);
    return true;
  }

  auto newSeg = segment<T>::make_segment(defaultSize_);
  new (newSeg->data_) T(val);
  newSeg->writeIdx_.store(1, std::memory_order_relaxed);
  newSeg->nextSeg_.store(nextSeg, std::memory_order_relaxed);
  curSeg->nextSeg_.store(newSeg, std::memory_order_relaxed);
  writeSeg_.store(newSeg, std::memory_order_release);
  return true;
}

template <typename T> std::optional<T> spsc_queue<T>::dequeue() {
  auto curSeg = readSeg_.load(std::memory_order_relaxed);
  auto curSegReadIdx = curSeg->readIdx_.load(std::memory_order_relaxed);
  auto curSegWriteIdx = curSeg->writeIdx_.load(std::memory_order_acquire);
  if (segment_has_data(curSegWriteIdx, curSegReadIdx)) {
    auto retVal = curSeg->data_[curSegReadIdx & sizeMask_];
    curSeg->readIdx_.store(curSegReadIdx + 1, std::memory_order_relaxed);
    return {retVal};
  }

  if (curSeg == writeSeg_.load(std::memory_order_relaxed)) {
    return std::nullopt;
  }
  auto nextSeg = curSeg->nextSeg_.load(std::memory_order_acquire);
  auto nextSegReadIdx = nextSeg->readIdx_.load(std::memory_order_relaxed);
  auto nextSegWriteIdx = nextSeg->writeIdx_.load(std::memory_order_relaxed);
  if (segment_has_data(nextSegWriteIdx, nextSegReadIdx)) {
    auto retVal = nextSeg->data_[nextSegReadIdx & sizeMask_];
    nextSeg->readIdx_.store(nextSegReadIdx + 1, std::memory_order_relaxed);
    readSeg_.store(nextSeg, std::memory_order_relaxed);
    return {retVal};
  }
  return std::nullopt;
}
template <typename T>
bool spsc_queue<T>::segment_has_free_slot(index_t writeIdx, index_t readIdx) {
  return (writeIdx - readIdx) < defaultSize_;
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

} // namespace kv_lockfree