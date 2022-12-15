//
// Created by Bach Dao.
//
#include "kv_lockfree/unbounded_spsc.h"
#include <gtest/gtest.h>
#include <iostream>
#include <thread>
template <typename T> void print(const T &msg) { std::cout << msg << "\n"; }
using namespace kv_lockfree;
TEST(ubounded_spsc, single_thread_put_then_get) {
  unbounded_spsc<int> q{4};
  size_t testRound = 128;
  for (int i = 0; i < testRound;) {
    if (q.enqueue(i)) {
      i++;
    }
  }
  int outVal;
  for (int i = 0; i < testRound;) {
    if (q.dequeue(outVal)) {
      EXPECT_EQ(outVal, i);
      i++;
    }
  }
}

TEST(ubounded_spsc, simultaneously_put_then_get) {
  unbounded_spsc<int> q{4};
  size_t testRound = 1024 * 1024;

  std::thread writer{[&] {
    for (int i = 0; i < testRound;) {
      if (q.enqueue(i)) {
        i++;
      }
    }
  }};
  std::thread reader{[&] {
    int outVal = -1;
    for (int i = 0; i < testRound;) {
      if (q.dequeue(outVal)) {
        EXPECT_EQ(outVal,i);
        i++;
      }
    }
  }};
  writer.join();
  reader.join();
}