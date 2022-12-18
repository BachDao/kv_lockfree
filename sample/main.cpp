#include "kv_lockfree/unbounded_spmc.h"
#include "kv_lockfree/unbounded_spsc.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

static uint64_t count_to(uint64_t end) {
  uint64_t result = 0;
  for (int i = 0; i < end; ++i) {
    result += i;
  }
  return result;
}
void unbound_spmc_test() {
  unbounded_spmc<int> queue{4};
  const size_t testRound = 1024;
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
  std::cout << "done" << std::endl;
}

int main() {
  unbound_spmc_test();
  return 0;
}