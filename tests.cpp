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

    return 0;
}
