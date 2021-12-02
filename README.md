# quickpool

![build status](https://github.com/tnagler/quickpool/actions/workflows/main.yml/badge.svg?branch=main)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/ed2deb06d4454ab3b488536426ec3066)](https://www.codacy.com/gh/tnagler/quickpool/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=tnagler/quickpool&amp;utm_campaign=Badge_Grade)
[![codecov](https://codecov.io/gh/tnagler/quickpool/branch/main/graph/badge.svg?token=ERPXZC8378)](https://codecov.io/gh/tnagler/quickpool)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Documentation](https://img.shields.io/website/http/tnagler.github.io/quickpool.svg)](https://vinecopulib.github.io/pyvinecopulib/)
[![DOI](https://zenodo.org/badge/427536398.svg)](https://zenodo.org/badge/latestdoi/427536398)


An easy-to-use, header-only work stealing thread pool in C++11.

## Why quickpool?

* **no install**: single-header library, 
  [C++11 compliant](https://en.cppreference.com/w/cpp/compiler_support), 
  no external dependencies.

* **fast**: Uses a [work stealing](https://en.wikipedia.org/wiki/Work_stealing) 
  queue with [lock-free](https://en.wikipedia.org/wiki/Non-blocking_algorithm#Lock-freedom) pops.

* **user friendly**: Dead simple [API](https://tnagler.github.io/quickpool/), 
  including direct access to a global pool.

* **light weight**: Less than 500 LOC including API documentation, comments, and
  whitespace.

## Usage

Basic usage is demonstrated below. See the 
[API documentation](https://tnagler.github.io/quickpool/) for more details.

### Static access to a global pool

The easiest method is to use the [`quickpool::push()`](https://tnagler.github.io/quickpool/namespacequickpool.html#affc41895dab281715c271aca3649e830), 
[`quickpool::async()`](https://tnagler.github.io/quickpool/namespacequickpool.html#a10575809d24ead3716e312585f90a94a), 
and [`quickpool::wait()`](https://tnagler.github.io/quickpool/namespacequickpool.html#a086671a25cc4f207112bc82a00688301) 
functions. They give access to a global thread pool that is only instantiated 
once with as many threads as there are cores.

```cpp
#include "quickpool.hpp"

quickpool::push([] { /* some work */ });
quickpool::push([] { /* some work */ });
quickpool::wait(); // waits for all current jobs to finish
```

If a task returns a result, use 
[`async()`](https://tnagler.github.io/quickpool/namespacequickpool.html#a10575809d24ead3716e312585f90a94a), 
which returns a [`std::future`](https://en.cppreference.com/w/cpp/thread/future) 
for the result.

```cpp
auto f = quickpool::async([] { return 1 + 1; });
// do something else ...
auto result = f.get();  // waits until done and returns result
```

Both [`push()`](https://tnagler.github.io/quickpool/namespacequickpool.html#affc41895dab281715c271aca3649e830)
and [`async()`](https://tnagler.github.io/quickpool/namespacequickpool.html#a10575809d24ead3716e312585f90a94a) 
can be called with extra arguments passed to the function.

```cpp
auto work = [] (const std::string& title, int i) { 
  std::cout << title << ": " << i << std::endl; 
};
quickpool::push(work, "first title", 5);
quickpool::async(work, "other title", 99);
quickpool::wait();
```

### Local thread pool

A [`ThreadPool`](https://tnagler.github.io/quickpool/classquickpool_1_1ThreadPool.html) 
can be set up manually, with an arbitrary number of threads. When the pool 
goes out of scope, all threads joined.

```cpp
{
  quickpool::ThreadPool pool(2);  // thread pool with two threads
  pool.push([] {});
  pool.async([] {});
  pool.wait();
}
// threads are joined
```
