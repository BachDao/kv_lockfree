#include "kv_lockfree/unbounded_spsc.h"
#include <thread>
int main() {
  kv_lockfree::unbounded_spsc<int> q{4};
  size_t testRound = 1024 * 1024 * 128;

  std::thread reader{[&] {
    int outVal = -1;
    for (int i = 0; i < testRound;) {
      if (q.dequeue(outVal)) {
        assert(outVal == i);
        i++;
      }
    }
  }};

  std::thread writer{[&] {
    for (int i = 0; i < testRound;) {
      if (q.enqueue(i)) {
        i++;
      }
    }
  }};

  writer.join();
  reader.join();
  return 0;
}