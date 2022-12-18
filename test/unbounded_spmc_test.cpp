//
// Created by Bach Dao.
//
#include "kv_lockfree/unbounded_spmc.h"
#include <gtest/gtest.h>
#include <thread>

using namespace kv_lockfree;
TEST(unbounded_spmc, sequential_put_then_get) {
  unbounded_spmc<int> queue{4};
  queue.enqueue(1);
  queue.enqueue(2);
  int outVal;
  queue.dequeue(outVal);
  assert(outVal == 1);
  queue.enqueue(3);
  queue.dequeue(outVal);
  assert(outVal == 2);
}
static uint64_t count_to(uint64_t end) {
  uint64_t result = 0;
  for (int i = 0; i < end; ++i) {
    result += i;
  }
  return result;
}
TEST(unbounded_spmc_test, sequential_test) {
  unbounded_spmc<int> queue{4};
  size_t testRound = 1024 * 1024;
  for (int i = 0; i < testRound; ++i) {
    queue.enqueue(i);
  }
  int prevVal = -1;
  int outVal = 0;
  for (int i = 0; i < testRound; ++i) {
    if (queue.dequeue(outVal)) {
      assert(outVal - 1 == prevVal);
      prevVal = outVal;
    }
  }
}
TEST(unbounded_spmc, simultaneously_put_and_get) {
  unbounded_spmc<int> queue{4};
  const size_t testRound = 1024 * 1024;
  const size_t workerCount = 8;
  std::vector<std::thread> workers;
  ;
  std::atomic<uint64_t> counter;
  std::thread writer{[&] {
    for (int i = 0; i < testRound;) {
      if (queue.enqueue(i))
        i++;
    }
  }};
  for (int i = 0; i < workerCount; ++i) {
    workers.emplace_back([&] {
      int outVal;
      for (int j = 0; j < testRound / workerCount;) {
        if (queue.dequeue(outVal)) {
          j++;
          counter.fetch_add(outVal);
        }
      }
    });
  }
  writer.join();
  for (auto &w : workers) {
    w.join();
  }
  EXPECT_EQ(counter.load(), count_to(testRound));
}