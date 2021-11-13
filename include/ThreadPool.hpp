#pragma once

#include "FinishLine.hpp"
#include "TaskManager.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <vector>

namespace tpool {

//! Implementation of the thread pool pattern based on `std::thread`.
class ThreadPool
{
  public:
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool();
    explicit ThreadPool(size_t nThreads);

    ~ThreadPool() noexcept;

    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&& other) = delete;

    static ThreadPool& global_instance()
    {
        static ThreadPool instance_;
        return instance_;
    }

    template<class Function, class... Args>
    void push(Function&& f, Args&&... args);

    template<class Function, class... Args>
    auto async(Function&& f, Args&&... args)
      -> std::future<decltype(f(args...))>;

    void wait();
    void clear();

  private:
    void start_worker(size_t id);
    void join_workers();
    void join();

    std::vector<std::thread> workers_;
    detail::TaskManager task_manager_;
    size_t n_workers_;
    std::exception_ptr error_ptr_{ nullptr };
    FinishLine finish_line_{ 0 };
};

//! constructs a thread pool with as many workers as there are cores.
inline ThreadPool::ThreadPool()
  : ThreadPool(std::thread::hardware_concurrency())
{}

//! constructs a thread pool with `nThreads` threads.
//! @param n_workers Number of worker threads to create; if `nThreads = 0`,
//! all
//!    work pushed to the pool will be done in the main thread.
inline ThreadPool::ThreadPool(size_t n_workers)
  : n_workers_{ n_workers }
{
    for (size_t id = 0; id != n_workers; ++id) {
        this->start_worker(id);
    }
}

//! destructor joins all threads if possible.
inline ThreadPool::~ThreadPool() noexcept
{
    // destructors should never throw
    try {
        task_manager_.stop();
        this->join_workers();
    } catch (...) {
    }
}

template<class Function, class... Args>
void
ThreadPool::push(Function&& f, Args&&... args)
{
    task_manager_.push(
      std::bind(std::forward<Function>(f), std::forward<Args>(args)...));
}

template<class Function, class... Args>
auto
ThreadPool::async(Function&& f, Args&&... args)
  -> std::future<decltype(f(args...))>
{
    using task = std::packaged_task<decltype(f(args...))()>;
    auto pack =
      std::bind(std::forward<Function>(f), std::forward<Args>(args)...);
    auto task_ptr = std::make_shared<task>(std::move(pack));
    task_manager_.push([task_ptr] { (*task_ptr)(); });
    return task_ptr->get_future();
}

void
ThreadPool::wait()
{
    while (!task_manager_.empty())
        finish_line_.wait();
}

void
ThreadPool::clear()
{
    task_manager_.clear();
}

//! waits for all jobs to finish and joins all threads.
inline void
ThreadPool::join()
{
    task_manager_.stop();
    this->join_workers();
}

//! spawns a worker thread waiting for jobs to arrive.
inline void
ThreadPool::start_worker(size_t id)
{
    workers_.emplace_back([this, id] {
        std::function<void()> task;
        while (!task_manager_.stopped()) {
            task_manager_.wait_for_jobs();
            finish_line_.start();
            while (task_manager_.try_pop(task))
                task();
            finish_line_.cross();
        }
    });
}

//! joins worker threads if possible.
inline void
ThreadPool::join_workers()
{
    if (n_workers_ > 0) {
        for (auto& worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
    }
}

} // end namespace tpool
