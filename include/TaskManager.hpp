// Copyright © 2021 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

// The following is heavily inspired by
// https://github.com/ConorWilliams/ConcurrentDeque.

#pragma once

#include "FinishLine.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace tpool {

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
    TaskQueue(size_t capacity = 256);

    // move/copy is not supported
    TaskQueue(TaskQueue const& other) = delete;
    TaskQueue& operator=(TaskQueue const& other) = delete;

    //! queries the size.
    size_t size() const;

    //! queries the capacity.
    size_t capacity() const;

    //! checks if queue is empty.
    bool empty() const;

    //! clears the queue.
    void clear();

    //! pushes a task to the bottom of the queue; enlarges the queue if full.
    void push(Task&& tsk);

    //! pops a task from the top of the queue. Returns an empty task
    //! when lost race.
    bool try_pop(Task& task);

  private:
    alignas(64) std::atomic_ptrdiff_t top_{ 0 };
    alignas(64) std::atomic_ptrdiff_t bottom_{ 0 };
    alignas(64) std::vector<detail::RingBuffer> buffers_;
    alignas(64) std::atomic_size_t buffer_index_{ 0 };

    // convenience aliases
    static constexpr std::memory_order m_relaxed = std::memory_order_relaxed;
    static constexpr std::memory_order m_acquire = std::memory_order_acquire;
    static constexpr std::memory_order m_release = std::memory_order_release;
    static constexpr std::memory_order m_seq_cst = std::memory_order_seq_cst;
};

TaskQueue::TaskQueue(size_t capacity)
{
    buffers_.emplace_back(capacity);
}

size_t
TaskQueue::size() const
{
    auto b = bottom_.load(m_relaxed);
    auto t = top_.load(m_relaxed);
    return static_cast<size_t>(b >= t ? b - t : 0);
}

size_t
TaskQueue::capacity() const
{
    return buffers_[buffer_index_].capacity();
}

bool
TaskQueue::empty() const
{
    return (this->size() == 0);
}

void
TaskQueue::clear()
{
    auto b = bottom_.load(m_relaxed);
    top_.store(b, m_release);
}

void
TaskQueue::push(Task&& tsk)
{
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
}

bool
TaskQueue::try_pop(Task& task)
{
    auto t = top_.load(m_acquire);
    std::atomic_thread_fence(m_seq_cst);
    auto b = bottom_.load(m_acquire);

    if (t < b) {
        // Must load *before* acquiring the slot as slot may be overwritten
        // immediately after acquiring. This load is NOT required to be atomic
        // even-though it may race with an overrite as we only return the value
        // if we win the race below garanteeing we had no race during our read.
        // If we loose the race then 'x' could be corrupt due to
        // read-during-write race but as T is trivially destructible this does
        // not matter.
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

struct TaskManager
{
    TaskQueue q_;
    std::mutex m_;
    std::condition_variable cv_;
    std::atomic_bool stopped_{ false };

    template<typename Task>
    void push(Task&& task)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push(task);
        }
        cv_.notify_one();
    }

    bool try_pop(std::function<void()>& task) { return q_.try_pop(task); }

    void clear() { q_.clear(); }

    bool empty() { return q_.empty(); }

    void wait_for_jobs()
    {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this] { return !q_.empty() || stopped_; });
    }

    bool stopped() { return stopped_; }
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

} // end namespace tpool
