#include "include/ThreadPool.hpp"
#include <iostream>

int main() {
    auto& pool = tpool::ThreadPool::global_instance();
    pool.push([] { std::cout << "test" << std::endl; });
    return 0;
}

// std::vector<std::vector<double>> x(size, std::vector<double>(200, 1));

// void
// check_equal_to(const std::vector<double>& x, double target)
// {
//     auto count = 0;
//     for (const auto& xx : x) {
//         // std::cout << xx << std::endl;
//         if (xx != target) {
//             count++;
//         }
//     }
//     if (count > 0) {
//         std::cout << count << " values incorrect\n";
//         return;
//     }
// }

// static void
// BM_MY_schedule(benchmark::State& state)
// {
//     std::vector<double> y(x.size());
//     auto target = x[0].size();
//     while (state.KeepRunning()) {
//         tpool::scheduler::global_instance().parallel_for(
//           0, x.size(), [&](int i) {
//               double sum{ 0 };
//               for (int j = 0; j <= 10; j++)
//                   sum += x[i][j];
//               y[i] = sum;
//               benchmark::DoNotOptimize(sum);
//               // std::this_thread::sleep_for(std::chrono::microseconds(3));
//           });
//         // check_equal_to(y, target);
//     }
// }

// static void
// BM_MY_unlocked_omp(benchmark::State& state)
// {
//     std::vector<double> y(x.size());
//     auto target = x[0].size();
//     while (state.KeepRunning()) {
//         tpool::unlocked::parallel_for_omp(0, x.size(), [&](int i) {
//             double sum{ 0 };
//             for (int j = 0; j <= 10; j++)
//                 sum += x[i][j];
//             y[i] = sum;
//             benchmark::DoNotOptimize(sum);
//             // std::this_thread::sleep_for(std::chrono::microseconds(3));
//         });
//         // check_equal_to(y, 1);
//     }
// }

// static void
// BM_MY_lockfree_omp(benchmark::State& state)
// {
//     std::vector<double> y(x.size());
//     auto target = x[0].size();
//     while (state.KeepRunning()) {
//         tpool::lockfree::parallel_for_omp(0, x.size(), [&](int i) {
//             double sum{ 0 };
//             for (int j = 0; j <= 10; j++)
//                 sum += x[i][j];
//             y[i] = sum;
//             benchmark::DoNotOptimize(sum);
//             // std::this_thread::sleep_for(std::chrono::microseconds(3));
//         });
//         // check_equal_to(y, 1);
//     }
// }

// static void
// BM_Durand(benchmark::State& state)
// {
//     std::vector<double> y(x.size());
//     auto target = x[0].size();
//     while (state.KeepRunning()) {
//         tpool::durand::parallel_for_omp(0, x.size(), [&](int i) {
//             double sum{ 0 };
//             for (int j = 0; j <= 10; j++)
//                 sum += x[i][j];
//             y[i] = sum;
//             benchmark::DoNotOptimize(sum);
//             // std::this_thread::sleep_for(std::chrono::microseconds(3));
//         });
//         // check_equal_to(y, 1);
//     }
// }

// static void
// BM_OMP_static(benchmark::State& state)
// {
//     std::vector<double> y(x.size());
//     auto target = x[0].size();
//     while (state.KeepRunning()) {
// #pragma omp parallel for schedule(static)
//         for (size_t i = 0; i < x.size(); i++) {
//             double sum{ 0 };
//             for (int j = 0; j <= 10; j++)
//                 sum += x[i][j];
//             benchmark::DoNotOptimize(sum);
//             y[i] = sum;
//             // std::this_thread::sleep_for(std::chrono::microseconds(3));
//         }
//         // check_equal_to(y, target);
//     }
// }

// static void
// BM_OMP_static_chunked(benchmark::State& state)
// {
//     std::vector<double> y(x.size());
//     auto target = x[0].size();
//     while (state.KeepRunning()) {
// #pragma omp parallel for schedule(static, 10)
//         for (size_t i = 0; i < x.size(); i++) {
//             double sum{ 0 };
//             for (int j = 0; j <= 10; j++)
//                 sum += x[i][j];
//             benchmark::DoNotOptimize(sum);
//             y[i] = sum;
//             // std::this_thread::sleep_for(std::chrono::microseconds(3));
//         }
//         // check_equal_to(y, target);
//     }
// }

// static void
// BM_OMP_dynamic(benchmark::State& state)
// {
//     std::vector<double> y(x.size());
//     auto target = x[0].size();
//     while (state.KeepRunning()) {
// #pragma omp parallel for schedule(dynamic, 1)
//         for (size_t i = 0; i < x.size(); i++) {
//             double sum{ 0 };
//             for (int j = 0; j <= 10; j++)
//                 sum += x[i][j];
//             benchmark::DoNotOptimize(sum);
//             y[i] = sum;
//             // std::this_thread::sleep_for(std::chrono::microseconds(3));
//         }
//         // check_equal_to(y, target);
//     }
// }

// static void
// BM_OMP_dynamic_chunked(benchmark::State& state)
// {
//     std::vector<double> y(x.size());
//     auto target = x[0].size();
//     while (state.KeepRunning()) {
// #pragma omp parallel for schedule(dynamic, 10)
//         for (size_t i = 0; i < x.size(); i++) {
//             double sum{ 0 };
//             for (int j = 0; j <= 10; j++)
//                 sum += x[i][j];
//             benchmark::DoNotOptimize(sum);
//             y[i] = sum;
//             // std::this_thread::sleep_for(std::chrono::microseconds(3));
//         }
//         // check_equal_to(y, target);
//     }
// }

// static void
// BM_OMP_dynamic_guided(benchmark::State& state)
// {
//     std::vector<double> y(x.size());
//     auto target = x[0].size();
//     while (state.KeepRunning()) {
// #pragma omp parallel for schedule(guided)
//         for (size_t i = 0; i < x.size(); i++) {
//             double sum{ 0 };
//             for (int j = 0; j <= 10; j++)
//                 sum += x[i][j];
//             benchmark::DoNotOptimize(sum);
//             y[i] = sum;
//             // std::this_thread::sleep_for(std::chrono::microseconds(3));
//         }
//         // check_equal_to(y, target);
//     }
// }


// static void
// BM_sequential(benchmark::State& state)
// {
//     std::vector<double> y(x.size());
//     auto target = x[0].size();
//     while (state.KeepRunning()) {
//         for (size_t i = 0; i < x.size(); i++) {
//             double sum{ 0 };
//             for (int j = 0; j <= 10; j++)
//                 sum += x[i][j];
//             benchmark::DoNotOptimize(sum);
//             y[i] = sum;
//             // std::this_thread::sleep_for(std::chrono::microseconds(3));
//         }
//         // check_equal_to(y, target);
//     }
// }

// const auto iter = 100000;

// BENCHMARK(BM_MY_schedule)
//   ->Iterations(iter)
//   ->Threads(1)
//   ->UseRealTime()
//   ->Unit(benchmark::kMicrosecond);

// BENCHMARK(BM_Durand)->Iterations(iter)->Threads(1)->UseRealTime()->Unit(
//   benchmark::kMicrosecond);
// // BENCHMARK(BM_MY_unlocked_omp)
// //   ->Iterations(iter)
// //   ->Threads(1)
// //   ->UseRealTime()
// //   ->Unit(benchmark::kMicrosecond);
// BENCHMARK(BM_MY_lockfree_omp)
//   ->Iterations(iter)
//   ->Threads(1)
//   ->UseRealTime()
//   ->Unit(benchmark::kMicrosecond);
// BENCHMARK(BM_sequential)
//   ->Iterations(iter)
//   ->Threads(1)
//   ->UseRealTime()
//   ->Unit(benchmark::kMicrosecond);

// // BENCHMARK(BM_OMP_static)->Iterations(iter);
// BENCHMARK(BM_OMP_static)->Iterations(iter)
//   ->Threads(1)
//   ->UseRealTime()
//   ->Unit(benchmark::kMicrosecond);
// // BENCHMARK(BM_OMP_dynamic)->Iterations(iter);
// // BENCHMARK(BM_OMP_dynamic_chunked)->Iterations(iter);
// // BENCHMARK(BM_OMP_dynamic_guided)->Iterations(iter);

// BENCHMARK_MAIN();
