#include "include/Static.hpp"
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

    return 0;
}
