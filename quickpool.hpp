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

//! quickpool namespace
namespace quickpool {

namespace detail {

static constexpr std::memory_order m_relaxed = std::memory_order_relaxed;
static constexpr std::memory_order m_acquire = std::memory_order_acquire;
static constexpr std::memory_order m_release = std::memory_order_release;
static constexpr std::memory_order m_seq_cst = std::memory_order_seq_cst;

template<typename T>
class aligned_atomic : public std::atomic<T>
{
  public:
    explicit aligned_atomic(T value)
      : std::atomic<T>(value)
    {}

  private:
    using TT = std::atomic<T>;
    char padding_[64 > sizeof(TT) ? 64 - sizeof(TT) : 1];
};

// forward declarations
template<typename T>
struct Block;

template<typename T>
struct Mempool;

// Storage slot.
template<typename T>
struct Slot
{
    alignas(alignof(T)) char storage[sizeof(T)];
    Block<T>* mother_block;
    bool done{ false };

    void operator()()
    {
        reinterpret_cast<T*>(&storage)->operator()();
        this->destruct();
        done = true;
        mother_block->free_one();
    }
    void clear()
    {
        if (!done) {
            reinterpret_cast<T*>(&storage)->~T();
        } else {
            done = false;
        }
    }
};

// Block of task slots.
template<typename T>
struct Block
{
    detail::aligned_atomic<uint16_t> num_freed{ 0 };
    uint16_t idx{ 0 };
    Block<T>* next{ nullptr };
    Block<T>* prev{ nullptr };
    const size_t size;

    std::unique_ptr<Slot<T>[]> slots;
    Mempool<T>* mother_pool;

    Block(uint16_t size = 1000)
      : size{ size }
      , slots{ std::unique_ptr<Slot<T>[]>(new Slot<T>[size]) }
    {
        for (size_t i = 0; i < size; ++i) {
            slots[i].mother_block = this;
        }
    }

    Slot<T>* get_slot()
    {
        if (idx++ < size) {
            return &slots[idx++];
        } else {
            return nullptr;
        }
    }

    void free_one() { num_freed.fetch_add(1, m_release); }

    void free_all()
    {
        idx = 0;
        num_freed.store(0, m_relaxed);
    }
};

template<typename T>
struct Mempool
{
    Block<T>* head;
    Block<T>* tail;
    const size_t block_size;

    Mempool(size_t block_size = 1000)
      : head{ new Block<T>(block_size) }
      , tail{ head }
      , block_size{ block_size }
    {}

    template<typename... Args>
    Slot<T>* allocate(Args&&... args)
    {
        Slot<T>* slot = get_slot();
        new (&slot->storage) T(std::forward<T>(args)...);
        return slot;
    }

    Slot<T>* get_slot()
    {
        // try to get memory slot in current block.
        if (auto slot = head->get_slot())
            return slot;

        // see if there's a free block ahead
        if (head->next != nullptr) {
            head = head->next;
            return head->get_slot();
        }

        // see if there are free'd blocks to collect
        auto old_tail = tail;
        while (tail->num_freed == block_size) {
            tail = tail->next;
        }
        if (tail != old_tail) {
            tail->prev->next = nullptr; // detach range [old_tail, tail)
            tail->prev = nullptr;       // ...
            this->set_head(old_tail);   // move to head
            return head->get_slot();
        }

        // create a new block and put and end of list.
        this->set_head(new Block<T>(block_size));
        return head->get_slot();
    }

    void set_head(Block<T>* block)
    {
        block->prev = head;
        head->next = block;
        head = block;
    }

    void reset()
    {
        auto block = head;
        do {
            block->free_all();
            block = block->next;
        } while (block != nullptr);
        head = tail;
    }

    ~Mempool()
    {
        while (tail->next) {
            tail = tail->next;
            delete tail->prev;
        }
        delete tail;
    }
};

}

//! @brief Todo list - a synchronization primitive.
//! @details Add a task with `add()`, cross it off with `cross()`, and wait for
//! all tasks to complete with `wait()`.
class TodoList
{
  public:
    //! constructs the todo list.
    //! @param num_tasks initial number of tasks.
    TodoList(size_t num_tasks = 0) noexcept
      : num_tasks_{ static_cast<int>(num_tasks) }
    {}

    //! adds tasks to the list.
    //! @param num_tasks add that many tasks to the list.
    void add(size_t num_tasks = 1) noexcept
    {
        num_tasks_.fetch_add(static_cast<int>(num_tasks), detail::m_release);
    }

    //! crosses tasks from the list.
    //! @param num_tasks cross that many tasks to the list.
    void cross(size_t num_tasks = 1)
    {
        num_tasks_.fetch_sub(static_cast<int>(num_tasks), detail::m_release);
        if (num_tasks_.load(detail::m_acquire) <= 0) {
            {
                std::lock_guard<std::mutex> lk(mtx_); // must lock before signal
            }
            cv_.notify_all();
        }
    }

    //! checks whether list is empty.
    bool empty() const noexcept
    {
        return num_tasks_.load(detail::m_acquire) <= 0;
    }

    //! waits for the list to be empty.
    //! @param millis if > 0; waiting aborts after waiting that many
    //! milliseconds.
    void wait(size_t millis = 0)
    {
        std::this_thread::yield();
        auto wake_up = [this] { return (num_tasks_ <= 0) || exception_ptr_; };
        std::unique_lock<std::mutex> lk(mtx_);
        if (millis == 0) {
            cv_.wait(lk, wake_up);
        } else {
            cv_.wait_for(lk, std::chrono::milliseconds(millis), wake_up);
        }
        if (exception_ptr_)
            std::rethrow_exception(exception_ptr_);
    }

    //! stops the list; it will forever look empty from now on.
    //! @param eptr (optional) pointer to an active exception to be rethrown by
    //! a waiting thread; typically retrieved from `std::current_exception()`.
    void stop(std::exception_ptr eptr = nullptr) noexcept
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            // Some threads may add() or cross() after we stop. The large
            // negative number prevents num_tasks_ from becoming positive again.
            num_tasks_.store(std::numeric_limits<int>::min() / 2);
            exception_ptr_ = eptr;
        }
        cv_.notify_all();
    }

    //! resets the todo list into initial state.
    void reset() noexcept
    {
        exception_ptr_ = nullptr;
        num_tasks_.store(0);
    }

  private:
    detail::aligned_atomic<int> num_tasks_{ 0 };
    std::mutex mtx_;
    std::condition_variable cv_;
    std::exception_ptr exception_ptr_{ nullptr };
};

//! Implementation details
namespace detail {

//! A simple ring buffer class.
template<typename T>
class RingBuffer
{
  public:
    explicit RingBuffer(size_t capacity)
      : buffer_{ std::unique_ptr<T[]>(new T[capacity]) }
      , capacity_{ capacity }
      , mask_{ capacity - 1 }
    {}

    size_t capacity() const { return capacity_; }

    void set_entry(size_t i, T val) { buffer_[i & mask_] = val; }

    T get_entry(size_t i) const { return buffer_[i & mask_]; }

    RingBuffer<T>* enlarged_copy(size_t bottom, size_t top) const
    {
        RingBuffer<T>* new_buffer = new RingBuffer{ 2 * capacity_ };
        for (size_t i = top; i != bottom; ++i)
            new_buffer->set_entry(i, this->get_entry(i));
        return new_buffer;
    }

  private:
    std::unique_ptr<T[]> buffer_;
    size_t capacity_;
    size_t mask_;
};

//! A multi-producer, multi-consumer queue; pops are lock free.
class TaskQueue
{
    // convenience aliases
    using Task = std::function<void()>;
    using TaskSlot = detail::Slot<Task>;

  public:
    //! constructs the queue with a given capacity.
    //! @param capacity must be a power of two.
    TaskQueue(size_t capacity = 256)
      : buffer_{ new RingBuffer<TaskSlot*>(capacity) }
    {}

    TaskQueue(TaskQueue const& other) = delete;
    TaskQueue& operator=(TaskQueue const& other) = delete;

    ~TaskQueue() { delete buffer_.load(); }

    //! checks if queue is empty.
    bool empty() const
    {
        return (bottom_.load(m_relaxed) <= top_.load(m_relaxed));
    }

    //! pushes a task to the bottom of the queue; returns false if queue is
    //! currently locked; enlarges the queue if full.
    bool try_push(Task&& task)
    {
        {
            // must hold lock in case of multiple producers, abort if already
            // taken, so we can check out next queue
            std::unique_lock<std::mutex> lk(mutex_, std::try_to_lock);
            if (!lk)
                return false;
            auto b = bottom_.load(m_relaxed);
            auto t = top_.load(m_acquire);
            RingBuffer<TaskSlot*>* buf_ptr = buffer_.load(m_relaxed);

            if (static_cast<int>(buf_ptr->capacity()) < (b - t) + 1) {
                // buffer is full, create enlarged copy before continuing
                auto old_buf = buf_ptr;
                buf_ptr = std::move(buf_ptr->enlarged_copy(b, t));
                old_buffers_.emplace_back(old_buf);
                buffer_.store(buf_ptr, m_relaxed);
            }

            buf_ptr->set_entry(b, mempool_.allocate(std::forward<Task>(task)));
            bottom_.store(b + 1, m_release);
        }
        cv_.notify_one();
        return true;
    }

    //! pops a task from the top of the queue; returns false if lost race.
    bool try_pop(TaskSlot& task)
    {
        auto t = top_.load(m_acquire);
        std::atomic_thread_fence(m_seq_cst);
        auto b = bottom_.load(m_acquire);

        if (t < b) {
            // must load task pointer before acquiring the slot, because it
            // could be overwritten immediately after
            auto task_ptr = buffer_.load(m_acquire)->get_entry(t);

            if (top_.compare_exchange_strong(t, t + 1, m_seq_cst, m_relaxed)) {
                task = std::move(*task_ptr); // won race, get task
                return true;
            }
        }
        return false; // queue is empty or lost race
    }

    //! waits for tasks or stop signal.
    void wait()
    {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return !this->empty() || stopped_; });
    }

    //! stops the queue and wakes up all workers waiting for jobs.
    void stop()
    {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    void reset()
    {
        mempool_.reset();
        top_.store(0);
        bottom_.store(0);
    }

  private:
    detail::aligned_atomic<int> top_{ 0 };
    detail::aligned_atomic<int> bottom_{ 0 };

    std::atomic<RingBuffer<TaskSlot*>*> buffer_{ nullptr };
    std::vector<std::unique_ptr<RingBuffer<TaskSlot*>>> old_buffers_;
    detail::Mempool<Task> mempool_;

    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_{ false };
};

//! Task manager based on work stealing
class TaskManager
{
  public:
    explicit TaskManager(size_t num_queues)
      : queues_{ new TaskQueue[num_queues] }
      , num_queues_{ num_queues }
      , owner_id_{ std::this_thread::get_id() }
    {}

    template<typename Task>
    void push(Task&& task)
    {
        rethrow_exception();
        todo_list_.add();
        while (status_ == Status::running) {
            if (queues_[push_idx_++ % num_queues_].try_push(task))
                return;
        }
    }

    template<typename Task>
    bool try_pop(Task& task, size_t worker_id = 0)
    {
        if (status_ != Status::running)
            return false;

        for (size_t k = 0; k <= num_queues_; k++) {
            if (queues_[(worker_id + k) % num_queues_].try_pop(task))
                return true;
        }
        return false;
    }

    void wait_for_jobs(size_t id)
    {
        if (status_ == Status::errored) {
            // main thread may be waiting to reset the pool
            std::lock_guard<std::mutex> lk(err_mtx_);
            if (++num_waiting_ == num_queues_)
                err_cv_.notify_all();
        } else {
            ++num_waiting_;
        }

        queues_[id].wait();
        --num_waiting_;
    }

    //! @param millis if > 0: stops waiting after millis ms
    void wait_for_finish(size_t millis = 0)
    {
        if (running())
            todo_list_.wait(millis);
        rethrow_exception();
    }

    bool called_from_owner_thread() const
    {
        return std::this_thread::get_id() == owner_id_;
    }

    bool done() const { return todo_list_.empty(); }

    void report_success() { todo_list_.cross(); }

    void report_fail(std::exception_ptr err_ptr)
    {
        if (!errored()) { // only catch first exception
            std::lock_guard<std::mutex> lk(err_mtx_);
            if (errored()) // double check
                return;
            err_ptr_ = err_ptr;
            status_ = Status::errored;
            todo_list_.stop();
        }
    }

    void stop()
    {
        status_ = Status::stopped;
        todo_list_.stop();
        // Worker threads wait on queue-specific mutex -> notify all queues.
        for (int i = 0; i < num_queues_; ++i)
            queues_[i].stop();
    }

    void rethrow_exception()
    {
        if (called_from_owner_thread() && errored()) {
            // wait for all threads to idle
            std::unique_lock<std::mutex> lk(err_mtx_);
            err_cv_.wait(lk, [this] { return num_waiting_ == num_queues_; });
            lk.unlock();

            // before throwing: restore defaults for potential future use
            todo_list_.reset();
            for (int i = 0; i < num_queues_; ++i)
                queues_[i].reset();
            status_ = Status::running;
            auto current_exception = err_ptr_;
            err_ptr_ = nullptr;
            std::rethrow_exception(current_exception);
        }
    }

    bool running() { return status_ == Status::running; }
    bool errored() { return status_ == Status::errored; }
    bool stopped() { return status_ == Status::stopped; }

  private:
    std::unique_ptr<TaskQueue[]> queues_;
    size_t num_queues_;
    const std::thread::id owner_id_;

    // task management
    detail::aligned_atomic<size_t> num_waiting_{ 0 };
    detail::aligned_atomic<size_t> push_idx_{ 0 };
    TodoList todo_list_{ 0 };
    std::exception_ptr err_ptr_{ nullptr };

    // synchronization in case of error to make pool reusable
    enum class Status
    {
        running,
        errored,
        stopped
    };
    std::atomic<Status> status_{ Status::running };
    std::mutex err_mtx_;
    std::condition_variable err_cv_;
};

} // end namespace detail

//! A work stealing thread pool.
class ThreadPool
{
  public:
    //! constructs a thread pool with as many workers as there are cores.
    ThreadPool()
      : ThreadPool(std::thread::hardware_concurrency())
    {}

    //! constructs a thread pool.
    //! @param n_workers number of worker threads to create; defaults to
    //! number of available (virtual) hardware cores.
    explicit ThreadPool(size_t n_workers)
      : task_manager_{ n_workers }
    {
        for (size_t id = 0; id < n_workers; ++id) {
            workers_.emplace_back([this, id] {
                detail::Slot<std::function<void()>> task;
                while (!task_manager_.stopped()) {
                    task_manager_.wait_for_jobs(id);
                    do {
                        // inner while to save cash misses when calling done()
                        while (task_manager_.try_pop(task, id))
                            this->execute_safely(task);
                    } while (!task_manager_.done());
                }
            });
        }
    }

    ~ThreadPool()
    {
        task_manager_.stop();
        for (auto& worker : workers_) {
            if (worker.joinable())
                worker.join();
        }
    }

    ThreadPool(ThreadPool&&) = delete;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&& other) = delete;

    //! @brief returns a reference to the global thread pool instance.
    static ThreadPool& global_instance()
    {
#ifdef _WIN32
        // Must leak resource, because windows + R deadlock otherwise. Memory
        // is released on shutdown.
        static auto ptr = new ThreadPool;
        return *ptr;
#else
        static ThreadPool instance_;
        return instance_;
#endif
    }

    //! @brief pushes a job to the thread pool.
    //! @param f a function.
    //! @param args (optional) arguments passed to `f`.
    template<class Function, class... Args>
    void push(Function&& f, Args&&... args)
    {
        if (workers_.size() == 0)
            return f(args...);
        task_manager_.push(
          std::bind(std::forward<Function>(f), std::forward<Args>(args)...));
    }

    //! @brief executes a job asynchronously the global thread pool.
    //! @param f a function.
    //! @param args (optional) arguments passed to `f`.
    //! @return A `std::future` for the task. Call `future.get()` to retrieve
    //! the results at a later point in time (blocking).
    template<class Function, class... Args>
    auto async(Function&& f, Args&&... args)
      -> std::future<decltype(f(args...))>
    {
        auto pack =
          std::bind(std::forward<Function>(f), std::forward<Args>(args)...);
        using pack_t = std::packaged_task<decltype(f(args...))()>;
        auto task_ptr = std::make_shared<pack_t>(std::move(pack));
        this->push([task_ptr] { (*task_ptr)(); });
        return task_ptr->get_future();
    }

    //! @brief waits for all jobs currently running on the global thread pool.
    void wait() { task_manager_.wait_for_finish(); }

  private:
    template<typename Task>
    void execute_safely(Task& task)
    {
        try {
            task();
            task_manager_.report_success();
        } catch (...) {
            task_manager_.report_fail(std::current_exception());
        }
    }

    detail::TaskManager task_manager_;
    std::vector<std::thread> workers_;
};

//! Direct access to the global thread pool -------------------

//! @brief push a job to the global thread pool.
//! @param f a function.
//! @param args (optional) arguments passed to `f`.
template<class Function, class... Args>
void
push(Function&& f, Args&&... args)
{
    ThreadPool::global_instance().push(std::forward<Function>(f),
                                       std::forward<Args>(args)...);
}

//! @brief executes a job asynchronously the global thread pool.
//! @param f a function.
//! @param args (optional) arguments passed to `f`.
//! @return A `std::future` for the task. Call `future.get()` to retrieve the
//! results at a later point in time (blocking).
template<class Function, class... Args>
auto
async(Function&& f, Args&&... args) -> std::future<decltype(f(args...))>
{
    return ThreadPool::global_instance().async(std::forward<Function>(f),
                                               std::forward<Args>(args)...);
}

//! @brief waits for all jobs currently running on the global thread pool.
inline void
wait()
{
    ThreadPool::global_instance().wait();
}

} // end namespace quickpool
