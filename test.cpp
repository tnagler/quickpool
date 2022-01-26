#include <iostream>

#include "quickpool.hpp"

int
main()
{
    using namespace quickpool;
    aligned::atomic<loop::State> test{};

    aligned::allocator<int, 64> alloc;
    std::cout << "* [quickpool] lock free: " << (test.is_lock_free() ? "yes\n" : "no\n");

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
            auto fun = [&](size_t i) { x[i] = 2 * x[i]; };
            parallel_for(0, x.size(), fun);

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
            pool.parallel_for(0, x.size(), fun);

            count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 4);
            if (count_wrong > 0) {
                for (auto xx : x)
                    std::cout << xx;
                std::cout << std::endl;
                throw std::runtime_error("parallel_for gives wrong result");
            }
            // std::cout << "OK" << std::endl;
        }

        // nested parallel_for()
        {
            // std::cout << "      * nested parallel_for: ";
            std::vector<std::vector<double>> x(100);
            for (auto& xx : x)
                xx = std::vector<double>(100, 1.0);
            parallel_for(0, x.size(), [&](int i) {
                parallel_for(0, x[i].size(), [&x, i](int j) { x[i][j] *= 2; });
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
            pool.parallel_for(0, x.size(), [&](int i) {
                pool.parallel_for(
                  0, x[i].size(), [&x, i](int j) { x[i][j] *= 2; });
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
            pool.wait();

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0)
                throw std::runtime_error("single threaded gives wrong result");
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

    return 0;
}
