#include <iostream>

#include "quickpool.hpp"

int
main()
{
    auto runs = 1000;
    for (auto run = 0; run < runs; run++) {
        std::cout << "* [quickpool] unit tests: run " << run + 1 << "/" << runs << "\t\r"
                  << std::flush;

        // thread pool push
        {
            // std::cout << "      * push: ";
            std::vector<size_t> x(10000, 1);
            for (size_t i = 0; i < x.size(); i++)
                quickpool::push([&](size_t i) -> void { x[i] = 2 * x[i]; }, i);
            quickpool::wait();

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++) {
                if (count_wrong += (x[i] != 2))
                    std::cout << x[i];
            }
            if (count_wrong > 0) {
                throw std::runtime_error("static push gives wrong result");
            }

            quickpool::ThreadPool pool;
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
                fut[i] = quickpool::async(dummy, i);
            for (size_t i = 0; i < x.size(); i++)
                x[i] = fut[i].get();
            quickpool::wait();

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0)
                throw std::runtime_error("static async gives wrong result");

            quickpool::ThreadPool pool;
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
            quickpool::parallel_for(0, x.size(), fun, 2);

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

            quickpool::ThreadPool pool;
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

        // parallel_for_each()
        {
            // std::cout << "      * parallel_for_each: ";

            std::vector<size_t> x(10000, 1);
            auto fun = [](size_t& xx) { xx = 2 * xx; };
            quickpool::parallel_for_each(x, fun);

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0) {
                for (auto xx : x)
                    std::cout << xx;
                throw std::runtime_error(
                  "static parallel_for_each gives wrong result");
            }

            quickpool::ThreadPool pool;
            pool.parallel_for_each(x, fun);

            count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 4);
            if (count_wrong > 0)
                throw std::runtime_error(
                  "parallel_for_each gives wrong result");

            // std::cout << "OK" << std::endl;
        }

        // single threaded
        {
            // std::cout << "      * single threaded: ";
            quickpool::ThreadPool pool(0);
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
            quickpool::ThreadPool pool;
            // pool passes exceptions either via wait() or push()
            std::exception_ptr eptr = nullptr;
            try {
                pool.push([] { throw std::runtime_error("test error"); });
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
    }

    std::cout << "- unit tests: OK              " << std::endl;

    return 0;
}
