// Copyright © 2021 Thomas Nagler
//
// This file is part of the RcppThread and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// RcppThread or https://github.com/tnagler/RcppThread/blob/master/LICENSE.md.

// The following is heavily inspired by
// https://github.com/ConorWilliams/ConcurrentDeque.

#pragma once

#include <cassert>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <exception>

namespace tpool {

class FinishLine
{
  public:
    FinishLine(size_t runners) noexcept
      : runners_(runners)
    {}

    void wait() noexcept
    {
        std::unique_lock<std::mutex> lk(mtx_);
        while ((runners_ > 0) & !exception_ptr_)
            cv_.wait(lk);
        if (exception_ptr_)
            std::rethrow_exception(exception_ptr_);
    }

    void add() noexcept { ++runners_; }

    void cross() noexcept
    {
        if (--runners_ == 0) {
            {
                std::lock_guard<std::mutex> lk(mtx_);
            }
            cv_.notify_all();
        }
    }

    void abort(std::exception_ptr e) noexcept
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            exception_ptr_ = e;
        }
    }

  private:
    alignas(64) std::atomic<size_t> runners_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::exception_ptr exception_ptr_{ nullptr };
};


}