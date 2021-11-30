#include <iostream>

#include "quickpool.hpp"

int
main()
{
    // {

    //     quickpool::detail::Mempool mempool;
    //     quickpool::detail::Task fun;
    //     auto ptr = mempool.allocate([] { std::cout << "test" << std::endl; });
    //     ptr->operator()();
    //     fun = std::move(*ptr);
    //     std::unique_ptr<quickpool::detail::Task[]> funs{
    //         new quickpool::detail::Task[100000]
    //     };
    //     for (int i = 0; i < 2000; ++i) {
    //         funs[i] = std::move(*mempool.allocate([] {}));
    //     }
    //     std::cout << "done allocating" << std::endl;
    //     for (int i = 0; i < 2000; ++i) {
    //         funs[i]();
    //     }
    //     for (int i = 0; i < 10000; ++i) {
    //         funs[i] = std::move(*mempool.allocate([] {}));
    //     }
    // }

    // README contents --------------------------------------------
    std::cout << "- Running contents from README: ";

    // Static access to a global pool
    {
        quickpool::push([] { /* some work */ });
        quickpool::push([] { /* some work */ });
        quickpool::wait(); // waits for all current jobs to finish
    }

    // async
    {
        auto f = quickpool::async([] { return 1 + 1; });
        // do something else ...
        auto result = f.get(); // waits until done and returns result
    }
    quickpool::wait();

    // extra arguments
    {
        auto work = [](const std::string& title, int i) {
            // std::cout << title << ": " << i << std::endl;
        };
        quickpool::push(work, "first title", 5);
        quickpool::async(work, "other title", 99);
        quickpool::wait();
    }

    // Local thread pool
    {
        quickpool::ThreadPool pool; // thread pool with two threads
        pool.push([] { /* some work */ });
        // pool.async([] { /* some work */ });
        pool.wait(); // waits for all current jobs to finish
    }

    // Task synchronization
    {
        std::vector<double> x(2); // shared resource
        quickpool::TodoList todo_prod(2);
        quickpool::TodoList todo_cons(2);

        auto job_prod = [&](int i, double val) {
            x.at(i) = val;
            todo_prod.cross();
        };
        auto job_cons = [&](int i) {
            todo_prod.wait(); // waits for all producers to finish
            // std::cout << x.at(i) << std::endl;
            todo_cons.cross();
        };

        quickpool::push(job_prod, 0, 1.337); // writes x[0]
        quickpool::push(job_prod, 1, 3.14);  // writes x[1]
        quickpool::push(job_cons, 0);        // reads x[0]
        quickpool::push(job_cons, 1);        // reads x[1]
        todo_cons.wait(); // waits for all consumers to finish
    }
    std::cout << "OK" << std::endl;
    quickpool::wait();

    // unit tests ---------------------------------------
    std::cout << "- unit tests:        \t\r";
    auto runs = 100;
    for (auto run = 0; run < runs; ++run) {
        std::cout << "- unit tests: run " << run + 1 << "/" << runs << "\t\r"
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
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                for (size_t i = 0; i < 10; i++) {
                    pool.push([&] {});
                }
            } catch (...) {
                eptr = std::current_exception();
            }

            if (!eptr) {
                throw std::runtime_error("exception not rethrown");
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
                throw std::runtime_error("exception not rethrown");
            } else {
                eptr = nullptr;
            }
            // std::cout << "OK" << std::endl;
        }
    }

    std::cout << "- unit tests: OK              " << std::endl;

    return 0;
}
