//
// Created by Bach Dao.
//
#include "kv_lockfree/bounded_spsc.h"
#include <gtest/gtest.h>
#include <thread>
using namespace kv_lockfree;
TEST(bounded_spsc, single_thread_put_then_get) {
  bounded_spsc<int> queue(4);
  queue.enqueue(1);
  int result = 0;
  queue.dequeue(result);
  EXPECT_EQ(result, 1);
}

TEST(bounded_spsc, reader_writer_simultaneously) {
  bounded_spsc<int> queue{4};
  size_t testRound = 8;
  std::thread writer{[&] {
    for (int i = 0; i < testRound;) {
      if (queue.enqueue(i)) {
        i++;
      }
    }
  }};
  std::thread reader{[&] {
    int outVal = -1;
    for (int i = 0; i < testRound;) {
      if (queue.dequeue(outVal)) {
        EXPECT_EQ(outVal, i);
        i++;
      }
    }
  }};
  writer.join();
  reader.join();
}