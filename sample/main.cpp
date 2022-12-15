#include "kv_lockfree/unbounded_spsc.h"
#include <cassert>
#include <iostream>
#include <thread>

void unbounded_test() {
  kv_lockfree::unbounded_spsc<uint64_t> q{4};

  std::thread reader{[&] {
    uint64_t outVal = -1;
    uint64_t index = 0;
    while (true) {
      if (q.dequeue(outVal)) {
        assert(outVal == index);
        index++;
      }
    }
  }};

  std::thread writer{[&] {
    uint64_t index = 0;
    while (true) {
      if (q.enqueue(index)) {
        index++;
      }
    }
  }};

  writer.join();
  reader.join();
  std::cout << "done" << std::endl;
}

int main() {
  unbounded_test();
  return 0;
}