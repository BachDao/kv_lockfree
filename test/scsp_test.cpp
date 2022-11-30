//#include "spmc.hpp"
//#include "spsc_counter.hpp"
//#include "spsc_queue.hpp"
//#include <chrono>
//#include <functional>
//#include <iostream>
//#include <thread>
//
//// measure runtime of function in microsecond
//template <typename Fn, typename... Args>
//auto measure(Fn &&fn, Args &&...args, std::string testCase) {
//  auto start = std::chrono::high_resolution_clock::now();
//  std::invoke(fn, std::forward<Args>(args)...);
//  auto end = std::chrono::high_resolution_clock::now();
//  auto duration = end - start;
//  return std::chrono::duration_cast<std::chrono::microseconds>(duration)
//      .count();
//}
//
//size_t totalRound = 1024 * 1024;
//size_t segmentDefaultSize = 1024 * 1024;
//size_t batchReadSize = 4;
//
//template <typename T> void write_only_test(T &queue) {
//  for (int i = 0; i < totalRound;) {
//    auto success = queue.enqueue(i);
//    if (success)
//      i++;
//  }
//}
//template <typename T> void read_only_test(T &queue) {
//  int retVal;
//  for (int i = 0; i < totalRound;) {
//    auto success = queue.dequeue(retVal);
//    i++;
//  }
//}
//
//template <typename T> void log(T msg) { std::cout << msg << "\n"; }
//
//template <typename T, typename... Ts> void log(T v, Ts... vs) {
//  std::cout << v;
//  log(vs...);
//}
//
//int main() {
//
//  //  double elapsed;
//  //  size_t ops;
//  //
//  //  //  elapsed = measure(write_only_test);
//  //  //   ops = (totalRound / elapsed);
//  //  //  log("write only: ", ops, " million per sec");
//  //
//  //  elapsed = measure(read_only_test);
//  //  ops = (totalRound / elapsed);
//  //  log("read only: ", ops, " million per sec");
//  //  kv_lockfree::spmc_queue<int> spmcQueue(1, 2);
//
//  kv_lockfree::spsc_counter<int> counterQueue(segmentDefaultSize);
//  auto elapsed =
//      measure([&] { write_only_test(counterQueue); }, "write only: ");
//  log((totalRound / elapsed));
//  kv_lockfree::spmc_queue<int> queue(1024);
//  elapsed = measure([&] { write_only_test(queue); }, "read only: ");
//  log((totalRound / elapsed));
//
//  return 0;
//}