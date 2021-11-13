# tpool

![build status](https://github.com/tnagler/tpool/actions/workflows/main.yml/badge.svg?branch=main)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/ed2deb06d4454ab3b488536426ec3066)](https://www.codacy.com/gh/tnagler/tpool/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=tnagler/tpool&amp;utm_campaign=Badge_Grade)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Documentation](https://img.shields.io/website/http/tnagler.github.io/tpool.svg)](https://vinecopulib.github.io/pyvinecopulib/)


An easy-to-use, header-only work stealing thread pool in C++11.

## Why tpool?

* **no install**: [C++11 compliant](https://en.cppreference.com/w/cpp/compiler_support) single-header library, just drop into your project and enjoy.

* **fast**: Uses a [work stealing](https://en.wikipedia.org/wiki/Work_stealing)  queue with [lock-free](https://en.wikipedia.org/wiki/Non-blocking_algorithm#Lock-freedom) pops.

* **user friendly**: Dead simple [API](https://tnagler.github.io/tpool/), including direct access to a global pool.

* **light weight**: Less than 500 LOC including API documentation and whitespace.

## Usage

Basic usage is demonstrated below. See the [API documentation](https://tnagler.github.io/tpool/) for more details.

### Static access to a global pool

The easiest method is to use the static [`tpool::push()`](https://tnagler.github.io/tpool/namespacetpool.html#affc41895dab281715c271aca3649e830), 
[`tpool::async()`](https://tnagler.github.io/tpool/namespacetpool.html#a10575809d24ead3716e312585f90a94a), 
and [`tpool::wait()`](https://tnagler.github.io/tpool/namespacetpool.html#a086671a25cc4f207112bc82a00688301) functions. They give access to a global thread pool that is only instantiated once with as many threads as there are cores.

```cpp
#include "tpool.hpp"

tpool::push([] { /* some work */ });
tpool::push([] { /* some work */ });
tpool::wait(); // waits for all current jobs to finish
```

If a task also returns a result, use [`async()`](https://tnagler.github.io/tpool/namespacetpool.html#a10575809d24ead3716e312585f90a94a), which returns a [`std::future`](https://en.cppreference.com/w/cpp/thread/future) for the result.

```cpp
auto f = tpool::async([] { return 1 + 1; });
// do something else ...
auto result = f.get();  // waits until done and returns result
```

Both [`push()`](https://tnagler.github.io/tpool/namespacetpool.html#affc41895dab281715c271aca3649e830) and [`async()`](https://tnagler.github.io/tpool/namespacetpool.html#a10575809d24ead3716e312585f90a94a) can also be called with extra arguments passed to the function.

```cpp
auto work = [] (const std::string& title, int i) { 
  std::cout << title << ": " << i << std::endl; 
};
tpool::push(work, "first title", 5);
tpool::async(work, "other title", 99);
tpool::wait();
```

### Local thread pool

A [`ThreadPool`](https://tnagler.github.io/tpool/classtpool_1_1ThreadPool.html) can also be set up manually, with an arbitrary number of threads. When the pool goes out of scope, all threads joined.

```cpp
{
  tpool::ThreadPool pool(2);  // thread pool with two threads
  pool.push([] {});
  pool.async([] {});
  pool.wait();
}
// threads are joined
```

### Task synchronization

In general, the pool may process the tasks in any order. Synchronization between tasks (e.g., one thread waiting intermediate results) must be done manually. Standard tools are [`std::mutex`](https://en.cppreference.com/w/cpp/thread/mutex) and [`std::condition_variable`](https://en.cppreference.com/w/cpp/thread/condition_variable). `tpool` exposes another synchronization primitive, [`FinishLine`](https://tnagler.github.io/tpool/classtpool_1_1FinishLine.html), that may be useful.

```cpp

// expect two producers and two consumers
tpool::FinishLine finish_prod(2);  
tpool::FinishLine finish_cons(2);

std::vector<double> x(2);
auto job_prod = [&] (int i, double val) { 
  x.at(i) = val; 
  finish_prod.cross(); 
};
auto job_cons = [&] (int i, double val) { 
  finish_prod.wait();            // waits for all producers to be done
  std::cout << x.at(i) << std::endl; 
  finish_cons.cross();
};

tpool::push(job_prod, 0, 1.337); // writes x[0]
tpool::push(job_prod, 1, 3.14);  // writes x[1]
tpool::push(job_cons, 0);        // reads x[0]
tpool::push(job_cons, 1);        // reads x[1]
finish_cons.wait();              // waits for all consumers to be done
```

If the number of runners is not known up front, you can start a runner on the fly using [`FinishLine::start()`](https://tnagler.github.io/tpool/classtpool_1_1FinishLine.html).
