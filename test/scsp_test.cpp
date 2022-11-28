#include "spsc_queue.hpp"
#include <chrono>
#include <iostream>
#include <thread>

template <typename Fn> auto measure(Fn &&fn) {
  auto start = std::chrono::high_resolution_clock::now();
  std::invoke(fn);
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = end - start;
  return std::chrono::duration_cast<std::chrono::microseconds>(duration)
      .count();
}

void random_delay() {
  auto random = std::rand() % 10;
  if (random > 5)
    return;
  std::this_thread::sleep_for(std::chrono::milliseconds(random));
}

void sequential_push_single_value() {
  kv_lockfree::bounded_spsc_queue<int> buffer(1024);
  buffer.push(1);
  auto result = buffer.pop();
  assert(result.has_value());
  assert(result.value() == 1);
}

void sequential_push_multiple_value() {
  kv_lockfree::bounded_spsc_queue<int> buffer(1024);
  for (int i = 0; i < 1024; ++i) {
    buffer.push(i);
  }
  for (int i = 0; i < 1024; ++i) {
    auto val = buffer.pop();
    assert(val == i);
  }
}

void produce_consume_test() {
  constexpr size_t size = 1024 * 1024;
  constexpr size_t round = 1024 * 1024 * 128;

  kv_lockfree::bounded_spsc_queue<int> buffer(size, 1);
  std::thread producer([&] {
    for (int i = 0; i < round;) {
      if (buffer.push(i)) {
        i++;
      }
    }
  });
  std::thread consumer([&] {
    for (int i = 0; i < round;) {
      auto val = buffer.pop();
      if (val) {
        assert(val.value() == i);
        i++;
      }
    }
  });
  producer.join();
  consumer.join();
}
size_t totalRound = 1024 * 1024 * 1024;
size_t segmentSize = 1024;
void unbounded_produce_consume() {
  kv_lockfree::spsc_queue<int> queue(segmentSize, 4);
  std::thread producer([&] {
    for (int i = 0; i < totalRound;) {
      if (queue.enqueue(i)) {
        i++;
      }
    }
  });
  std::thread consumer([&] {
    int outVal;
    for (int i = 0; i < totalRound;) {
      auto success = queue.dequeue(outVal);
      if (success) {
        assert(outVal == i);
        i++;
      }
    }
  });
  producer.join();
  consumer.join();
}

void unbounded_raw_add() {
  kv_lockfree::spsc_queue<size_t> queue(segmentSize);
  size_t val = 0;
  auto localRound = totalRound;
  while (localRound--) {
    queue.enqueue(val);
    val++;
  }
}

int main() {
  auto timeElapsed = measure(unbounded_raw_add);
  auto avg = (totalRound / timeElapsed);
  std::cout << "Raw add: " << avg << " million ops" << std::endl;

  //  unbounded_produce_consume();
  return 0;
}