// Copyright 2021 Thomas Nagler (MIT License)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace tpool {

class FinishLine
{
  public:
    FinishLine(size_t runners = 0) noexcept
      : runners_(runners)
    {}

    void add(size_t num = 1) noexcept { runners_ = runners_ + num; }
    void start() noexcept { ++runners_; }
    void cross() noexcept
    {
        if (--runners_ == 0)
            cv_.notify_all();
    }

    void wait() noexcept
    {
        std::unique_lock<std::mutex> lk(mtx_);
        while ((runners_ > 0) & !exception_ptr_)
            cv_.wait(lk);
        if (exception_ptr_)
            std::rethrow_exception(exception_ptr_);
    }

    void abort(std::exception_ptr e) noexcept
    {
        std::lock_guard<std::mutex> lk(mtx_);
        exception_ptr_ = e;
        cv_.notify_all();
    }

  private:
    alignas(64) std::atomic<size_t> runners_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::exception_ptr exception_ptr_{ nullptr };
};

namespace detail {

//! A simple ring buffer class.
class RingBuffer
{
    using Task = std::function<void()>;
    using TaskVec = std::vector<std::shared_ptr<Task>>;

  public:
    explicit RingBuffer(size_t capacity)
      : capacity_(capacity)
      , mask_(capacity - 1)
      , buffer_(TaskVec(capacity))
    {
        if (capacity_ & (capacity_ - 1))
            throw std::runtime_error("capacity must be a power of two");
    }

    size_t capacity() const { return capacity_; }

    void store(size_t i, Task&& tsk)
    {
        buffer_[i & mask_] = std::make_shared<Task>(std::move(tsk));
    }

    void copy(size_t i, std::shared_ptr<Task> task_ptr)
    {
        buffer_[i & mask_] = task_ptr;
    }

    std::shared_ptr<Task> load(size_t i) const { return buffer_[i & mask_]; }

    // creates a new ring buffer with shared pointers to current elements.
    RingBuffer enlarge(size_t bottom, size_t top) const
    {
        RingBuffer buffer(2 * capacity_);
        for (size_t i = top; i != bottom; ++i)
            buffer.copy(i, this->load(i));
        return buffer;
    }

  private:
    TaskVec buffer_;
    size_t capacity_;
    size_t mask_;
};

class TaskQueue
{
    using Task = std::function<void()>;

  public:
    //! constructs the que with a given capacity.
    //! @param capacity must be a power of two.
    TaskQueue(size_t capacity = 256) { buffers_.emplace_back(capacity); }

    // move/copy is not supported
    TaskQueue(TaskQueue const& other) = delete;
    TaskQueue& operator=(TaskQueue const& other) = delete;

    //! queries the size.
    size_t size() const
    {
        auto b = bottom_.load(m_relaxed);
        auto t = top_.load(m_relaxed);
        return static_cast<size_t>(b >= t ? b - t : 0);
    }

    //! queries the capacity.
    size_t capacity() const { return buffers_[buffer_index_].capacity(); }

    //! checks if queue is empty.
    bool empty() const { return (this->size() == 0); }

    //! clears the queue.
    void clear()
    {
        auto b = bottom_.load(m_relaxed);
        top_.store(b, m_release);
    }

    //! pushes a task to the bottom of the queue; enlarges the queue if full.
    bool try_push(Task&& tsk)
    {
        std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
        if (!lk)
            return false;
        auto b = bottom_.load(m_relaxed);
        auto t = top_.load(m_acquire);

        if (buffers_[buffer_index_].capacity() < (b - t) + 1) {
            // capacity reached, create copy with double size
            buffers_.push_back(buffers_[buffer_index_].enlarge(b, t));
            buffer_index_++;
        }
        buffers_[buffer_index_].store(b, std::move(tsk));

        std::atomic_thread_fence(m_release);
        bottom_.store(b + 1, m_relaxed);
        return true;
    }

    //! pops a task from the top of the queue. Returns an empty task
    //! when lost race.
    bool try_pop(Task& task)
    {
        auto t = top_.load(m_acquire);
        std::atomic_thread_fence(m_seq_cst);
        auto b = bottom_.load(m_acquire);

        if (t < b) {
            // Must load *before* acquiring the slot as slot may be overwritten
            // immediately after acquiring. This load is NOT required to be
            // atomic even-though it may race with an overrite as we only return
            // the value if we win the race below garanteeing we had no race
            // during our read. If we loose the race then 'x' could be corrupt
            // due to read-during-write race but as T is trivially destructible
            // this does not matter.
            auto task_ptr = buffers_[buffer_index_].load(t);

            if (top_.compare_exchange_strong(t, t + 1, m_seq_cst, m_relaxed)) {
                task = std::move(*task_ptr.get());
                return true;
            } else {
                return false; // lost race for this task
            }
        } else {
            return false; // queue is empty
        }
    }

  private:
    alignas(64) std::atomic_ptrdiff_t top_{ 0 };
    alignas(64) std::atomic_ptrdiff_t bottom_{ 0 };
    alignas(64) std::vector<detail::RingBuffer> buffers_;
    alignas(64) std::atomic_size_t buffer_index_{ 0 };

    std::mutex mutex_;

    // convenience aliases
    static constexpr std::memory_order m_relaxed = std::memory_order_relaxed;
    static constexpr std::memory_order m_acquire = std::memory_order_acquire;
    static constexpr std::memory_order m_release = std::memory_order_release;
    static constexpr std::memory_order m_seq_cst = std::memory_order_seq_cst;
};

struct TaskManager
{
    std::vector<TaskQueue> queues_;
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic_bool stopped_{ false };
    alignas(64) std::atomic_size_t push_idx_;
    alignas(64) std::atomic_size_t pop_idx_;
    size_t num_queues_;

    TaskManager(size_t num_queues = 1)
      : num_queues_{ num_queues }
    {
        queues_ = std::vector<TaskQueue>(num_queues);
    }

    template<typename Task>
    void push(Task&& task)
    {
        while (!queues_[push_idx_++ % num_queues_].try_push(task))
            continue;
        cv_.notify_one();
    }

    bool empty()
    {
        for (auto& q : queues_) {
            if (!q.empty())
                return false;
        }
        return true;
    }

    bool try_pop(std::function<void()>& task)
    {
        do {
            if (queues_[pop_idx_++ % num_queues_].try_pop(task))
                return true;
        } while (!this->empty());
        return false;
    }
    void clear()
    {
        for (auto& q : queues_)
            q.clear();
    }
    bool stopped() { return stopped_; }

    void wait_for_jobs()
    {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this] { return !this->empty() || stopped_; });
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            stopped_ = true;
        }
        cv_.notify_all();
    }
};

} // end namespace detail

class ThreadPool
{
  public:
    explicit ThreadPool(
      size_t num_threads = std::thread::hardware_concurrency());

    ThreadPool(ThreadPool&&) = delete;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&& other) = delete;
    ~ThreadPool() noexcept;

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
    template<class Task>
    void execute_safely(Task& task);

    std::vector<std::thread> workers_;
    detail::TaskManager task_manager_;
    size_t n_workers_;
    std::exception_ptr error_ptr_{ nullptr };
    FinishLine finish_line_{ 0 };
};


//! constructs a thread pool with `num_threads` threads.
//! @param n_workers Number of worker threads to create; defaults to number of 
//!    available (virtual) hardware cores.
inline ThreadPool::ThreadPool(size_t n_workers)
  : n_workers_{ n_workers }
{
    for (size_t id = 0; id < n_workers; ++id) {
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
                execute_safely(task);
            finish_line_.cross();
        }
    });
}

template<class Task>
inline void
ThreadPool::execute_safely(Task& task)
{
    try {
        task();
    } catch (...) {
        finish_line_.abort(std::current_exception());
    }
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

//! Static access to the global thread pool ------------------

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

static void
wait()
{
    ThreadPool::global_instance().wait();
}

static void
clear()
{
    ThreadPool::global_instance().clear();
}

}