#include "spsc_queue.hpp"
#include <iostream>
#include <thread>
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

void unbounded_produce_consume() {
  constexpr size_t defaultSize = 8;
  constexpr size_t round = 1024 * 1024 * 128;
  kv_lockfree::spsc_queue<int> queue(defaultSize);
  std::thread producer([&] {
    for (int i = 0; i < round;) {
      if (queue.enqueue(i)) {
        i++;
      }
    }
  });
  std::thread consumer([&] {
    for (int i = 0; i < round;) {
      auto val = queue.dequeue();
      if (val.has_value()) {
        assert(val.value() == i);
        i++;
      }
    }
  });
  producer.join();
  consumer.join();
}

int main() {
  //  sequential_push_single_value();
  //  sequential_push_multiple_value();
  //  produce_consume_test();
  unbounded_produce_consume();
  return 0;
}