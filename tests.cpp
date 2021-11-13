#include "tpool.hpp"
#include <iostream>

int
main()
{
    tpool::ThreadPool pool;
    pool.push([] { std::cout << "- push: "; });
    pool.wait();
    std::cout << "OK" << std::endl;

    tpool::push([] { std::cout << "- static push: "; });
    tpool::wait();
    std::cout << "OK" << std::endl;

    auto fut = tpool::async([] {
        std::cout << "- static async: ";
        return 1;
    });
    if (fut.get() == 1)
        std::cout << "OK" << std::endl;

    std::cout << "- scheduling: ";
    std::vector<int> x(100000, 0);
    for (auto& xx : x)
        tpool::push([&] { xx = 1; });
    tpool::wait();

    auto ok = true;
    for (auto& xx : x)
        ok = ok & (xx == 1);
    std::cout << (ok ? "OK" : "FAILED") << std::endl;


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
        tpool::ThreadPool pool(2); // thread pool with two threads
        pool.push([] { /* some work */ });
        pool.async([] { /* some work */ });
        pool.wait(); // waits for all current jobs to finish
    }

    // Task synchronization
    {
        std::vector<double> x(2); // shared resource
        // expect two threads to cross the line
        tpool::FinishLine finish_prod(2);
        tpool::FinishLine finish_cons(2);

        auto job_prod = [&](int i, double val) {
            x.at(i) = val;
            finish_prod.cross();
        };
        auto job_cons = [&](int i) {
            finish_prod.wait(); // waits for all producers to be done
            // std::cout << x.at(i) << std::endl;
            finish_cons.cross();
        };

        tpool::push(job_prod, 0, 1.337); // writes x[0]
        tpool::push(job_prod, 1, 3.14);  // writes x[1]
        tpool::push(job_cons, 0);        // reads x[0]
        tpool::push(job_cons, 1);        // reads x[1]
        finish_cons.wait();              // waits for all consumers to be done
    }
    std::cout << "OK" << std::endl;

    return 0;
}
