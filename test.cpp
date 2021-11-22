#include <iostream>

#include "tpool.hpp"

int
main()
{

    // README contents --------------------------------------------
    std::cout << "- Running contents from README: ";

    // Static access to a global pool
    {
        tpool::push([] { /* some work */ });
        tpool::push([] { /* some work */ });
        tpool::wait(); // waits for all current jobs to finish
    }

    // async
    {
        auto f = tpool::async([] { return 1 + 1; });
        // do something else ...
        auto result = f.get(); // waits until done and returns result
    }
    tpool::wait();

    // extra arguments
    {
        auto work = [](const std::string& title, int i) {
            // std::cout << title << ": " << i << std::endl;
        };
        tpool::push(work, "first title", 5);
        tpool::async(work, "other title", 99);
        tpool::wait();
    }

    // Local thread pool
    {
        tpool::ThreadPool pool; // thread pool with two threads
        pool.push([] { /* some work */ });
        pool.async([] { /* some work */ });
        pool.wait(); // waits for all current jobs to finish
    }

    // Task synchronization
    {
        std::vector<double> x(2); // shared resource
        tpool::TodoList todo_prod(2);
        tpool::TodoList todo_cons(2);

        auto job_prod = [&](int i, double val) {
            x.at(i) = val;
            todo_prod.cross();
        };
        auto job_cons = [&](int i) {
            todo_prod.wait(); // waits for all producers to finish
            // std::cout << x.at(i) << std::endl;
            todo_cons.cross();
        };

        tpool::push(job_prod, 0, 1.337); // writes x[0]
        tpool::push(job_prod, 1, 3.14);  // writes x[1]
        tpool::push(job_cons, 0);        // reads x[0]
        tpool::push(job_cons, 1);        // reads x[1]
        todo_cons.wait();                // waits for all consumers to finish
    }
    std::cout << "OK" << std::endl;
    tpool::wait();

    // unit tests ---------------------------------------
    std::cout << "- unit tests: " << std::endl;
    auto runs = 1;
    for (auto run = 0; run < runs; run++) {
        std::cout << "    * run " << run + 1 << "/" << runs << " -------------"
                  << std::endl;

        // thread pool push
        {
            std::cout << "      * push: ";
            std::vector<size_t> x(10000, 1);
            // auto dummy = [&](size_t i) -> void { x[i] = 2 * x[i]; };
            for (size_t i = 0; i < x.size(); i++)
                tpool::push([&](size_t i) -> void { x[i] = 2 * x[i]; }, i);
            tpool::wait();

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++) {
                if (count_wrong += (x[i] != 2))
                    std::cout << x[i];
            }
            if (count_wrong > 0) {
                throw std::runtime_error("static push gives wrong result");
            }

            tpool::ThreadPool pool;
            x = std::vector<size_t>(10000, 1);
            for (size_t i = 0; i < x.size(); i++)
                pool.push([&](size_t i) -> void { x[i] = 2 * x[i]; }, i);
            pool.wait();

            count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0)
                throw std::runtime_error("push gives wrong result");
            std::cout << "OK" << std::endl;
        }

        // async()
        {
            std::cout << "      * async: ";
            std::vector<size_t> x(10000, 1);
            auto dummy = [&](size_t i) { return 2 * x[i]; };

            std::vector<std::future<size_t>> fut(x.size());
            for (size_t i = 0; i < x.size(); i++)
                fut[i] = tpool::async(dummy, i);
            for (size_t i = 0; i < x.size(); i++)
                x[i] = fut[i].get();
            tpool::wait();

            size_t count_wrong = 0;
            for (size_t i = 0; i < x.size(); i++)
                count_wrong += (x[i] != 2);
            if (count_wrong > 0)
                throw std::runtime_error("static async gives wrong result");

            tpool::ThreadPool pool;
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
            std::cout << "OK" << std::endl;
        }

        // single threaded
        {
            std::cout << "      * single threaded: ";
            tpool::ThreadPool pool(0);
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
            std::cout << "OK" << std::endl;
        }

        // rethrows exceptions
        {
            std::cout << "      * exception handling: ";
            tpool::ThreadPool pool;
            for (size_t i = 0; i < 4; i++) {
                pool.push([&] { throw std::runtime_error("test"); });
            }
            try {
                pool.wait();
            } catch (const std::exception& e) {
                if (e.what() == std::string("test")) {
                    std::cout << "OK" << std::endl;
                } else {
                    throw std::runtime_error("exception not rethrown");
                }
            }
        }
    }

    return 0;
}
