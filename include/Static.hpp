#pragma once

#include "ThreadPool.hpp"

namespace tpool {

template<class Function, class... Args>
static void
push(Function&& f, Args&&... args)
{
    auto& global_pool = ThreadPool::global_instance();
    global_pool.push(std::forward<Function>(f), std::forward<Args>(args)...);
}

template<class Function, class... Args>
static auto
async(Function&& f, Args&&... args) -> std::future<decltype(f(args...))>
{
    auto& global_pool = ThreadPool::global_instance();
    return global_pool.async(std::forward<Function>(f),
                             std::forward<Args>(args)...);
}

static void wait() {
    ThreadPool::global_instance().wait();
}

static void clear() {
    ThreadPool::global_instance().clear();
}

}