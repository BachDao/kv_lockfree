//
// Created by Bach Dao.
//
#include "kv_lockfree/bounded_spmc.h"
#include <gtest/gtest.h>
#include <thread>

using namespace kv_lockfree;
TEST(bounded_spmc, sequential_test) {
  bounded_spmc<int> queue(8);
  assert(queue.enqueue(1));
  assert(queue.enqueue(2));
  assert(queue.enqueue(3));
  int outVal;
  assert(queue.dequeue(outVal));
  assert(outVal == 1);
  assert(queue.dequeue(outVal));
  assert(outVal == 2);
  assert(queue.dequeue(outVal));
  assert(outVal == 3);
}

int count_to(int n) {
  int result = 0;
  for (int i = 0; i < n; ++i) {
    result += i;
  }
  return result;
}

TEST(bounded_spmc, simultaneosly_put_and_get) {
  bounded_spmc<int> queue(128);
  size_t testRound = 1024 * 1024 * 8 ;
  size_t consumerCount = 8;
  int endResult = count_to(testRound);
  std::vector<std::thread> worker_;
  std::atomic<uint32_t> counter_;
  std::thread producer{[&] {
    for (int i = 0; i < testRound;) {
      if (queue.enqueue(i))
        i++;
    }
  }};

  for (int i = 0; i < consumerCount; ++i) {
    worker_.push_back(std::thread{[&] {
      int outVal;
      int localCounter = 0;
      for (int i = 0; i < testRound / consumerCount;) {
        if (queue.dequeue(outVal)) {
          i++;
          localCounter += outVal;
        }
      }
      counter_.fetch_add(localCounter);
    }});
  }
  producer.join();
  for (auto &w : worker_) {
    w.join();
  }
  assert(counter_.load() == endResult);
}