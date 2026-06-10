#include "quickpool.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <list>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

volatile std::uint64_t sink = 0;

struct Options
{
    int repetitions = 50;
    size_t max_threads = 0;
    bool quick = false;
};

struct Workload
{
    int push_tasks;
    int short_loop_items;
    int short_loop_repeats;
    int loop_items;
    int nested_outer;
    int nested_inner;
    int list_items;
};

void
usage(const char* name)
{
    std::cout << "usage: " << name
              << " [--quick] [--repetitions N] [--max-threads N]\n";
}

size_t
parse_size(const std::string& value)
{
    const auto parsed = std::strtoull(value.c_str(), nullptr, 10);
    if (parsed > std::numeric_limits<size_t>::max()) {
        throw std::out_of_range("value is too large");
    }
    return static_cast<size_t>(parsed);
}

Options
parse_options(int argc, char** argv)
{
    Options options;
    const auto hw = std::max(std::thread::hardware_concurrency(), 1u);
    options.max_threads = std::min<size_t>(hw, 8);

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--quick") {
            options.quick = true;
            options.repetitions = std::min(options.repetitions, 2);
            options.max_threads = std::min<size_t>(options.max_threads, 2);
        } else if (arg == "--repetitions" && i + 1 < argc) {
            options.repetitions = static_cast<int>(parse_size(argv[++i]));
        } else if (arg == "--max-threads" && i + 1 < argc) {
            options.max_threads = parse_size(argv[++i]);
        } else if (arg == "--help") {
            usage(argv[0]);
            std::exit(0);
        } else {
            usage(argv[0]);
            throw std::invalid_argument("unknown benchmark argument: " + arg);
        }
    }

    if (options.repetitions <= 0) {
        throw std::invalid_argument("repetitions must be positive");
    }
    return options;
}

Workload
workload_for(const Options& options)
{
    if (options.quick) {
        return Workload{ 1000, 3, 100, 4000, 40, 40, 2000 };
    }
    return Workload{ 10000, 3, 1000, 100000, 200, 200, 50000 };
}

std::vector<size_t>
thread_counts(size_t max_threads)
{
    std::vector<size_t> counts;
    counts.push_back(0);
    counts.push_back(1);
    if (max_threads >= 2) {
        counts.push_back(2);
    }
    if (max_threads > 2) {
        counts.push_back(max_threads);
    }
    counts.erase(std::unique(counts.begin(), counts.end()), counts.end());
    return counts;
}

std::uint64_t
burn(size_t rounds, std::uint64_t seed)
{
    auto x = seed + 0x9e3779b97f4a7c15ull;
    for (size_t i = 0; i < rounds; ++i) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        x *= 0x2545f4914f6cdd1dull;
    }
    return x;
}

template<class Function>
double
median_ms(int repetitions, Function f)
{
    std::vector<double> timings;
    timings.reserve(static_cast<size_t>(repetitions));
    for (int rep = 0; rep < repetitions; ++rep) {
        const auto start = std::chrono::steady_clock::now();
        f();
        const auto stop = std::chrono::steady_clock::now();
        const std::chrono::duration<double, std::milli> elapsed = stop - start;
        timings.push_back(elapsed.count());
    }
    std::sort(timings.begin(), timings.end());
    const auto middle = static_cast<size_t>(repetitions / 2);
    if (repetitions % 2 == 1) {
        return timings[middle];
    }
    return (timings[middle - 1] + timings[middle]) / 2.0;
}

void
print_result(const std::string& name,
             size_t threads,
             int items,
             int repetitions,
             double median)
{
    const auto ns_per_item = median * 1000000.0 / static_cast<double>(items);
    std::cout << name << ',' << threads << ',' << items << ',' << repetitions
              << ',' << std::fixed << std::setprecision(3) << median << ','
              << std::setprecision(1) << ns_per_item << '\n';
}

void
benchmark_push_empty(size_t threads, int tasks, int repetitions)
{
    quickpool::ThreadPool pool(threads);
    const auto median = median_ms(repetitions, [&] {
        std::atomic<int> done{ 0 };
        for (int i = 0; i < tasks; ++i) {
            pool.push([&] { done.fetch_add(1, std::memory_order_relaxed); });
        }
        pool.wait();
        if (done.load(std::memory_order_relaxed) != tasks) {
            throw std::runtime_error("push_empty lost work");
        }
    });
    print_result("push_empty", threads, tasks, repetitions, median);
}

void
benchmark_push_medium(size_t threads, int tasks, int repetitions)
{
    quickpool::ThreadPool pool(threads);
    std::vector<std::uint64_t> output(static_cast<size_t>(tasks));
    const auto median = median_ms(repetitions, [&] {
        for (int i = 0; i < tasks; ++i) {
            pool.push([&, i] {
                output[static_cast<size_t>(i)] =
                  burn(128, static_cast<std::uint64_t>(i));
            });
        }
        pool.wait();
    });
    for (auto value : output) {
        sink ^= value;
    }
    print_result("push_medium", threads, tasks, repetitions, median);
}

void
benchmark_parallel_for_short(size_t threads,
                             int items,
                             int short_repeats,
                             int repetitions)
{
    quickpool::ThreadPool pool(threads);
    std::vector<std::uint64_t> output(static_cast<size_t>(items));
    const auto median = median_ms(repetitions, [&] {
        for (int repeat = 0; repeat < short_repeats; ++repeat) {
            pool.parallel_for(0, items, [&, repeat](int i) {
                output[static_cast<size_t>(i)] =
                  burn(16, static_cast<std::uint64_t>(i + repeat));
            });
        }
    });
    for (auto value : output) {
        sink ^= value;
    }
    print_result(
      "parallel_for_short", threads, items * short_repeats, repetitions, median);
}

void
benchmark_parallel_for_tiny(size_t threads, int items, int repetitions)
{
    quickpool::ThreadPool pool(threads);
    std::vector<std::uint64_t> output(static_cast<size_t>(items));
    const auto median = median_ms(repetitions, [&] {
        pool.parallel_for(0, items, [&](int i) {
            output[static_cast<size_t>(i)] = static_cast<std::uint64_t>(i) + 1;
        });
    });
    for (auto value : output) {
        sink ^= value;
    }
    print_result("parallel_for_tiny", threads, items, repetitions, median);
}

void
benchmark_parallel_for_medium(size_t threads, int items, int repetitions)
{
    quickpool::ThreadPool pool(threads);
    std::vector<std::uint64_t> output(static_cast<size_t>(items));
    const auto median = median_ms(repetitions, [&] {
        pool.parallel_for(0, items, [&](int i) {
            output[static_cast<size_t>(i)] =
              burn(128, static_cast<std::uint64_t>(i));
        });
    });
    for (auto value : output) {
        sink ^= value;
    }
    print_result("parallel_for_medium", threads, items, repetitions, median);
}

void
benchmark_parallel_for_uneven(size_t threads, int items, int repetitions)
{
    quickpool::ThreadPool pool(threads);
    std::vector<std::uint64_t> output(static_cast<size_t>(items));
    const auto median = median_ms(repetitions, [&] {
        pool.parallel_for(0, items, [&](int i) {
            const auto idx = static_cast<size_t>(i);
            output[idx] = burn(16 + (idx % 64) * 4,
                               static_cast<std::uint64_t>(idx));
        });
    });
    for (auto value : output) {
        sink ^= value;
    }
    print_result("parallel_for_uneven", threads, items, repetitions, median);
}

void
benchmark_parallel_for_nested(size_t threads,
                              int outer,
                              int inner,
                              int repetitions)
{
    quickpool::ThreadPool pool(threads);
    const auto items = outer * inner;
    std::vector<std::uint64_t> output(static_cast<size_t>(items));
    const auto median = median_ms(repetitions, [&] {
        pool.parallel_for(0, outer, [&](int i) {
            pool.parallel_for(0, inner, [&, i](int j) {
                const auto idx = static_cast<size_t>(i * inner + j);
                output[idx] = burn(16, static_cast<std::uint64_t>(idx));
            });
        });
    });
    for (auto value : output) {
        sink ^= value;
    }
    print_result("parallel_for_nested", threads, items, repetitions, median);
}

void
benchmark_for_each_vector(size_t threads, int items, int repetitions)
{
    quickpool::ThreadPool pool(threads);
    std::vector<std::uint64_t> output(static_cast<size_t>(items));
    const auto median = median_ms(repetitions, [&] {
        pool.parallel_for_each(output, [](std::uint64_t& value) {
            value = burn(16, value + 1);
        });
    });
    for (auto value : output) {
        sink ^= value;
    }
    print_result("for_each_vector", threads, items, repetitions, median);
}

void
benchmark_for_each_list(size_t threads, int items, int repetitions)
{
    quickpool::ThreadPool pool(threads);
    std::list<std::uint64_t> output(static_cast<size_t>(items), 1);
    const auto median = median_ms(repetitions, [&] {
        pool.parallel_for_each(output, [](std::uint64_t& value) {
            value = burn(16, value + 1);
        });
    });
    for (auto value : output) {
        sink ^= value;
    }
    print_result("for_each_list", threads, items, repetitions, median);
}

} // namespace

int
main(int argc, char** argv)
{
    const auto options = parse_options(argc, argv);
    const auto workload = workload_for(options);
    const auto counts = thread_counts(options.max_threads);

    std::cout << "# quickpool benchmark\n";
    std::cout << "# max_threads=" << options.max_threads
              << ", repetitions=" << options.repetitions
              << ", quick=" << (options.quick ? "true" : "false") << '\n';
    std::cout << "name,threads,items,repetitions,median_ms,ns_per_item\n";

    for (auto threads : counts) {
        benchmark_push_empty(threads, workload.push_tasks, options.repetitions);
        benchmark_push_medium(threads, workload.push_tasks, options.repetitions);
        benchmark_parallel_for_short(threads,
                                     workload.short_loop_items,
                                     workload.short_loop_repeats,
                                     options.repetitions);
        benchmark_parallel_for_tiny(
          threads, workload.loop_items, options.repetitions);
        benchmark_parallel_for_medium(
          threads, workload.loop_items, options.repetitions);
        benchmark_parallel_for_uneven(
          threads, workload.loop_items, options.repetitions);
        benchmark_parallel_for_nested(threads,
                                      workload.nested_outer,
                                      workload.nested_inner,
                                      options.repetitions);
        benchmark_for_each_vector(
          threads, workload.loop_items, options.repetitions);
        benchmark_for_each_list(
          threads, workload.list_items, options.repetitions);
    }

    if (sink == 0) {
        std::cerr << "# sink=" << sink << '\n';
    }
    return 0;
}
