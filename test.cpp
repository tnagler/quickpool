#include <algorithm>
#include <chrono>
#include <atomic>
#include <iostream>
#include <list>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

#include "quickpool.hpp"

int
checked_size_int(size_t size)
{
    if (size > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("test range is too large");
    }
    return static_cast<int>(size);
}

struct ThrowsOnCopy
{
    ThrowsOnCopy() {}

    ThrowsOnCopy(const ThrowsOnCopy&)
    {
        throw std::runtime_error("copy failed");
    }

    void operator()() const {}
};

void
stress_queue_growth_and_reuse()
{
    using namespace quickpool;
    const auto batches = 8;
    const auto tasks = 1024;
    ThreadPool pool(1);

    for (auto batch = 0; batch < batches; ++batch) {
        std::atomic_bool release{ false };
        std::atomic_int done{ 0 };

        for (auto i = 0; i < tasks; ++i) {
            pool.push([&] {
                while (!release.load()) {
                    std::this_thread::yield();
                }
                done++;
            });
        }

        release = true;
        pool.wait();
        if (done != tasks) {
            throw std::runtime_error("queue growth stress lost work");
        }
    }
}

void
stress_concurrent_push_and_reuse()
{
    using namespace quickpool;
    const auto hardware = std::max(std::thread::hardware_concurrency(), 2u);
    const auto workers =
      std::min<size_t>(static_cast<size_t>(hardware), static_cast<size_t>(8));
    const auto producers = 4;
    const auto batches = 20;
    const auto tasks_per_producer = 512;
    ThreadPool pool(workers);

    for (auto batch = 0; batch < batches; ++batch) {
        std::atomic_bool start{ false };
        std::atomic_int ready{ 0 };
        std::atomic_int done{ 0 };
        std::vector<std::atomic_int> counts(static_cast<size_t>(producers));
        for (auto& count : counts) {
            count = 0;
        }
        std::vector<std::thread> producer_threads;
        producer_threads.reserve(static_cast<size_t>(producers));

        for (auto producer = 0; producer < producers; ++producer) {
            producer_threads.emplace_back([&, producer] {
                ready++;
                while (!start.load()) {
                    std::this_thread::yield();
                }

                for (auto task = 0; task < tasks_per_producer; ++task) {
                    pool.push([&, producer, task] {
                        if ((task % 8) == 0) {
                            std::this_thread::yield();
                        }
                        counts[static_cast<size_t>(producer)]++;
                        done++;
                    });
                    if ((task % 16) == 0) {
                        std::this_thread::yield();
                    }
                }
            });
        }

        while (ready != producers) {
            std::this_thread::yield();
        }
        start = true;

        for (auto& producer_thread : producer_threads) {
            producer_thread.join();
        }
        pool.wait();

        const auto expected = producers * tasks_per_producer;
        if (done != expected) {
            throw std::runtime_error("concurrent push stress lost work");
        }
        for (auto producer = 0; producer < producers; ++producer) {
            if (counts[static_cast<size_t>(producer)] != tasks_per_producer) {
                throw std::runtime_error(
                  "concurrent push stress has wrong producer count");
            }
        }
    }
}

int
main()
{
    using namespace quickpool;
    mem::aligned::atomic<loop::State> test{};
    std::cout << "* [quickpool] lock free: "
              << (test.is_lock_free() ? "yes\n" : "no\n");

    auto runs = 100;
    for (auto run = 0; run < runs; run++) {
        std::cout << "* [quickpool] unit tests: run " << run + 1 << "/" << runs
                  << "\t\r" << std::flush;

        // thread pool push
        {
            // std::cout << "      * push: ";
            std::vector<size_t> x(10000, 1);
            for (size_t i = 0; i < x.size(); i++)
                push([&](size_t i) -> void { x[i] = 2 * x[i]; }, i);
            wait();

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++) {
                if (count_wrong += (x[i] != 2))
                    std::cout << x[i];
            }
            if (count_wrong > 0) {
                throw std::runtime_error("static push gives wrong result");
            }

            ThreadPool pool;
            x = std::vector<size_t>(10000, 1);
            for (size_t i = 0; i < x.size(); i++)
                pool.push([&](size_t i) -> void { x[i] = 2 * x[i]; }, i);
            pool.wait();

            count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0)
                throw std::runtime_error("push gives wrong result");
            // std::cout << "OK" << std::endl;
        }

        // async()
        {
            // std::cout << "      * async: ";
            std::vector<size_t> x(10000, 1);
            auto dummy = [&](size_t i) { return 2 * x[i]; };

            std::vector<std::future<size_t>> fut(x.size());
            for (size_t i = 0; i < x.size(); i++)
                fut[i] = async(dummy, i);
            for (size_t i = 0; i < x.size(); i++)
                x[i] = fut[i].get();
            wait();

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0)
                throw std::runtime_error("static async gives wrong result");

            ThreadPool pool;
            x = std::vector<size_t>(10000, 1);
            std::vector<std::future<size_t>> fut2(x.size());
            for (size_t i = 0; i < x.size(); i++)
                fut2[i] = pool.async(dummy, i);
            for (size_t i = 0; i < x.size(); i++)
                x[i] = fut2[i].get();
            pool.wait();

            count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0)
                throw std::runtime_error("async gives wrong result");
            // std::cout << "OK" << std::endl;
        }

        // parallel_for()
        {
            // std::cout << "      * parallel_for: ";
            std::vector<size_t> x(10000, 1);
            auto fun = [&](int i) {
                auto idx = static_cast<size_t>(i);
                x[idx] = 2 * x[idx];
            };
            parallel_for(0, checked_size_int(x.size()), fun);

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0) {
                for (auto xx : x)
                    std::cout << xx;
                std::cout << std::endl;
                throw std::runtime_error(
                  "static parallel_for gives wrong result");
            }

            ThreadPool pool;
            pool.parallel_for(0, checked_size_int(x.size()), fun);

            count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 4);
            if (count_wrong > 0) {
                for (auto xx : x)
                    std::cout << xx;
                std::cout << std::endl;
                throw std::runtime_error("parallel_for gives wrong result");
            }

            int empty_count = 0;
            parallel_for(4, 4, [&](int) { empty_count++; });
            pool.parallel_for(8, 2, [&](int) { empty_count++; });
            if (empty_count != 0) {
                throw std::runtime_error("empty parallel_for runs work");
            }
            // std::cout << "OK" << std::endl;
        }

        // nested parallel_for()
        {
            // std::cout << "      * nested parallel_for: ";
            std::vector<std::vector<double>> x(100);
            for (auto& xx : x)
                xx = std::vector<double>(100, 1.0);
            parallel_for(0, checked_size_int(x.size()), [&](int i) {
                auto row = static_cast<size_t>(i);
                parallel_for(0,
                             checked_size_int(x[row].size()),
                             [&x, row](int j) {
                                 x[row][static_cast<size_t>(j)] *= 2;
                             });
            });

            size_t count_wrong = 0;
            for (auto xx : x) {
                for (auto xxx : xx)
                    count_wrong += xxx != 2;
            }
            if (count_wrong > 0) {
                throw std::runtime_error(
                  "static nested parallel_for gives wrong result");
            }

            ThreadPool pool;
            pool.parallel_for(0, checked_size_int(x.size()), [&](int i) {
                auto row = static_cast<size_t>(i);
                pool.parallel_for(
                  0, checked_size_int(x[row].size()), [&x, row](int j) {
                      x[row][static_cast<size_t>(j)] *= 2;
                  });
            });

            count_wrong = 0;
            for (auto xx : x) {
                for (auto xxx : xx)
                    count_wrong += xxx != 4;
            }
            if (count_wrong > 0) {
                throw std::runtime_error(
                  "nested parallel_for gives wrong result");
            }
            // std::cout << "OK" << std::endl;
        }

        // parallel_for_each()
        {
            // std::cout << "      * parallel_for_each: ";
            std::vector<size_t> x(10000, 1);
            auto fun = [](size_t& xx) { xx = 2 * xx; };
            parallel_for_each(x, fun);

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0) {
                for (auto xx : x)
                    std::cout << xx;
                throw std::runtime_error(
                  "static parallel_for_each gives wrong result");
            }

            ThreadPool pool;
            pool.parallel_for_each(x, fun);

            count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 4);
            if (count_wrong > 0)
                throw std::runtime_error(
                  "parallel_for_each gives wrong result");

            std::list<size_t> y(10000, 1);
            parallel_for_each(y, fun);

            count_wrong = 0;
            for (auto yy : y)
                count_wrong += (yy != 2);
            if (count_wrong > 0)
                throw std::runtime_error(
                  "static parallel_for_each list gives wrong result");

            pool.parallel_for_each(y, fun);

            count_wrong = 0;
            for (auto yy : y)
                count_wrong += (yy != 4);
            if (count_wrong > 0)
                throw std::runtime_error(
                  "parallel_for_each list gives wrong result");
            // std::cout << "OK" << std::endl;
        }

        // nested parallel_for_each()
        {
            // std::cout << "      * nested parallel_for_each: ";
            std::vector<std::vector<double>> x(100);
            for (auto& xx : x)
                xx = std::vector<double>(100, 1.0);
            parallel_for_each(x, [](std::vector<double>& xx) {
                parallel_for_each(xx, [](double& xxx) { xxx *= 2; });
            });

            size_t count_wrong = 0;
            for (auto xx : x) {
                for (auto xxx : xx)
                    count_wrong += xxx != 2;
            }
            if (count_wrong > 0) {
                throw std::runtime_error(
                  "static nested parallel_for_each gives wrong result");
            }

            ThreadPool pool;
            pool.parallel_for_each(x, [&](std::vector<double>& xx) {
                pool.parallel_for_each(xx, [](double& xxx) { xxx *= 2; });
            });

            count_wrong = 0;
            for (auto xx : x) {
                for (auto xxx : xx)
                    count_wrong += xxx != 4;
            }
            if (count_wrong > 0) {
                throw std::runtime_error(
                  "nested parallel_for_each gives wrong result");
            }
            // std::cout << "OK" << std::endl;
        }

        // single threaded
        {
            // std::cout << "      * single threaded: ";
            ThreadPool pool(0);
            std::vector<size_t> x(1000, 1);
            auto dummy = [&](size_t i) -> void { x[i] = 2 * x[i]; };

            for (size_t i = 0; i < x.size(); i++) {
                pool.push(dummy, i);
            }
            std::atomic_int non_void_push_ran{ 0 };
            pool.push([&] {
                non_void_push_ran++;
                return 1;
            });
            pool.wait();

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0)
                throw std::runtime_error("single threaded gives wrong result");
            if (non_void_push_ran != 1)
                throw std::runtime_error("single threaded non-void push failed");

            pool.parallel_for(0, checked_size_int(x.size()), [&](int i) {
                x[static_cast<size_t>(i)] += 1;
            });
            count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 3);
            if (count_wrong > 0) {
                throw std::runtime_error(
                  "single threaded parallel_for gives wrong result");
            }

            pool.parallel_for_each(x, [](size_t& xx) { xx += 1; });
            count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 4);
            if (count_wrong > 0) {
                throw std::runtime_error(
                  "single threaded parallel_for_each gives wrong result");
            }
            // std::cout << "OK" << std::endl;
        }

        // rethrows exceptions
        {
            // std::cout << "      * exception handling: ";
            ThreadPool pool;
            // pool passes exceptions either via wait() or push()
            std::exception_ptr eptr = nullptr;
            try {
                pool.push([] { throw std::runtime_error("test error"); });
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                for (size_t i = 0; i < 10; i++) {
                    pool.push([&] {});
                }
            } catch (...) {
                eptr = std::current_exception();
            }

            if (!eptr) {
                throw std::runtime_error("exception not rethrown by push");
            } else {
                eptr = nullptr;
            }

            // poool should be functional again
            pool.push([] { throw std::runtime_error("test error"); });
            try {
                pool.wait();
            } catch (...) {
                eptr = std::current_exception();
            }
            if (!eptr) {
                throw std::runtime_error("exception not rethrown by wait");
            } else {
                eptr = nullptr;
            }
            // std::cout << "OK" << std::endl;
        }

        // stop_and_reset()
        {
            ThreadPool pool(2);
            std::atomic_bool started{ false };
            std::atomic_bool release{ false };
            std::atomic_int finished{ 0 };

            pool.push([&] {
                started = true;
                while (!release.load()) {
                    std::this_thread::yield();
                }
                finished++;
            });
            while (!started.load()) {
                std::this_thread::yield();
            }

            std::exception_ptr eptr = nullptr;
            try {
                try {
                    throw std::runtime_error("test error");
                } catch (...) {
                    release = true;
                    pool.stop_and_reset();
                }
            } catch (...) {
                eptr = std::current_exception();
            }

            if (!eptr) {
                throw std::runtime_error(
                  "stop_and_reset does not rethrow pending exception");
            }
            if (finished != 1) {
                throw std::runtime_error(
                  "stop_and_reset does not wait for running work");
            }

            std::atomic_int reused{ 0 };
            pool.push([&] { reused++; });
            pool.wait();
            if (reused != 1) {
                throw std::runtime_error(
                  "stop_and_reset does not restore pool");
            }
        }

        // push exception safety
        {
            quickpool::sched::TaskManager manager(1);
            try {
                manager.push(ThrowsOnCopy{});
                throw std::runtime_error("copy failure was not thrown");
            } catch (const std::runtime_error&) {
            }
            if (!manager.done()) {
                throw std::runtime_error("failed push leaves unfinished work");
            }
        }

        // can be resized
        {
            // std::cout << "      * resizing: ";

            std::atomic_int dummy{ 0 };

            ThreadPool pool(2);
            pool.set_active_threads(1);
            for (int i = 0; i < 100; i++)
                pool.push([&] { dummy++; });
            pool.wait();
            if (dummy != 100) {
                throw std::runtime_error("downsizing doesn't work");
            }

            pool.set_active_threads(2);
            for (int i = 0; i < 100; i++)
                pool.push([&] { dummy++; });
            pool.wait();
            pool.wait();
            if (dummy != 200) {
                throw std::runtime_error("restore size doesn't work");
            }

            pool.set_active_threads(3);
            for (int i = 0; i < 100; i++)
                pool.push([&] { dummy++; });
            pool.wait();
            pool.wait();
            if (dummy != 300) {
                throw std::runtime_error("upsizing doesn't work");
            }

            ThreadPool busy_pool(1);
            std::atomic_int busy_resize_count{ 0 };
            for (int i = 0; i < 10; i++) {
                busy_pool.push([&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    busy_resize_count++;
                });
            }
            busy_pool.set_active_threads(2);
            if (busy_resize_count != 10) {
                throw std::runtime_error("busy upsizing drops work");
            }

            pool.set_active_threads(std::thread::hardware_concurrency() + 1);
            for (int i = 0; i < 100; i++)
                pool.push([&] { dummy++; });
            pool.wait();
            pool.wait();
            if (dummy != 400) {
                throw std::runtime_error("oversizing doesn't work");
            }

            // std::cout << "OK" << std::endl;
        }
    }

    std::cout << "* [quickpool] unit tests: OK              " << std::endl;

    std::cout << "* [quickpool] stress tests: queue growth\t\r"
              << std::flush;
    stress_queue_growth_and_reuse();
    std::cout << "* [quickpool] stress tests: concurrent push\t\r"
              << std::flush;
    stress_concurrent_push_and_reuse();
    std::cout << "* [quickpool] stress tests: OK              "
              << std::endl;

    return 0;
}
