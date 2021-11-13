// Copyright © 2021 Thomas Nagler
//
// This file is part of the tpool library and licensed under the terms of
// the MIT license. For a copy, see the LICENSE.md file in the root directory of
// tpool or https://github.com/tnagler/tpool/blob/main/LICENSE.md.

#pragma once

#include <condition_variable>
#include <mutex>
#include <atomic>
#include <exception>

namespace tpool {

class FinishLine
{
  public:
    FinishLine(size_t runners = 0) noexcept
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

    void add(size_t num = 1) noexcept { runners_ = runners_ + num; }

    void start() noexcept { ++runners_; }

    void cross() noexcept
    {
        if (--runners_ == 0)
            cv_.notify_all();
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