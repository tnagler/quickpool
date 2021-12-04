# quickpool

![build status](https://github.com/tnagler/quickpool/actions/workflows/main.yml/badge.svg?branch=main)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/ed2deb06d4454ab3b488536426ec3066)](https://www.codacy.com/gh/tnagler/quickpool/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=tnagler/quickpool&amp;utm_campaign=Badge_Grade)
[![codecov](https://codecov.io/gh/tnagler/quickpool/branch/main/graph/badge.svg?token=ERPXZC8378)](https://codecov.io/gh/tnagler/quickpool)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Documentation](https://img.shields.io/website/http/tnagler.github.io/quickpool.svg)](https://vinecopulib.github.io/pyvinecopulib/)
[![DOI](https://zenodo.org/badge/427536398.svg)](https://zenodo.org/badge/latestdoi/427536398)

> Fast and easy parallel computing in C++11

## Why quickpool?

### Developer friendly

The library consists of a [single header file](https://github.com/tnagler/quickpool/blob/parallel-for/quickpool.hpp) with permissive license. 
It requires only C++11 and is otherwise self-contained, no external dependencies.
Just drop `quickpool.hpp` in your project folder and enjoy.

### User friendly API

* [`push(f, args...)`](https://tnagler.github.io/quickpool/namespacequickpool.html#affc41895dab281715c271aca3649e830) schedules a task running `f(args...)` with no return,   
* [`async(f, args...)`](https://tnagler.github.io/quickpool/namespacequickpool.html#a10575809d24ead3716e312585f90a94a) schedules a task running `f(args...)` and returns an [`std::future`](https://en.cppreference.com/w/cpp/thread/future), 
* [`wait()`](https://tnagler.github.io/quickpool/namespacequickpool.html#a086671a25cc4f207112bc82a00688301) waits for all scheduled tasks to finish,
* [`parallel_for(b, e, f)`](https://tnagler.github.io/quickpool/namespacequickpool.html#aa72b140a64eabe34cd9302bab837c24c) runs `f(i)` for all `b <= i < e`,
* [`parallel_for_each(x, f)`](https://tnagler.github.io/quickpool/namespacequickpool.html#aeb91fe18664b8d06523aba081174abe3) runs `f(*it)` for all  `std::begin(x) <= it < std::end(x)`.

Loops can be nested, see the examples below. All functions 
dispatch to a global thread pool instantiated only once with as 
many threads as there are cores. Optionally, one can create a local `ThreadPool`
exposing the functions above. See also the [API documentation](https://tnagler.github.io/quickpool/).

### Cutting edge algorithms

All scheduling uses [work stealing](https://en.wikipedia.org/wiki/Work_stealing) synchronized by [cache-aligned atomic](https://github.com/tnagler/aligned_atomic) operations.

The thread pool assigns each worker thread a task queue. The workers process 
first their own queue and then steal work from others. The algorithm is [lock-free](https://en.wikipedia.org/wiki/Non-blocking_algorithm)
in the standard case where only a single thread pushes work to the pool. 

Parallel loops assign each worker part of the loop range.
When a worker completes its own range, it steals half the range
of another worker. This perfectly balances the load and only requires a logarithmic number of steals (= points of contention). The algorithm uses 
[double-wide compare-and-swap](https://en.wikipedia.org/wiki/Compare-and-swap#Extensions), which is lock-free on most modern processor
architectures.

## Examples


### Thread pool

```cpp
push([] { /* some work */ });
wait(); // wait for current jobs to finish

auto f = async([] { return 1 + 1; }); // get future for result
// do something else ...
auto result = f.get(); // waits until done and returns result
```

Both [`push()`](https://tnagler.github.io/quickpool/namespacequickpool.html#affc41895dab281715c271aca3649e830)
and [`async()`](https://tnagler.github.io/quickpool/namespacequickpool.html#a10575809d24ead3716e312585f90a94a) 
can be called with extra arguments passed to the function.

```cpp
auto work = [] (const std::string& title, int i) { 
  std::cout << title << ": " << i << std::endl; 
};
push(work, "first title", 5);
async(work, "other title", 99);
wait();
```

### Parallel loops

Existing sequential loops are easy to parallelize:
```cpp
std::vector<double> x(10, 1);

// sequential version
for (int i = 0; i < x.size(); ++i) 
  x[i] *= 2;

// parallel version
parallel_for(0, x.size(), [&] (int i) { x[i] *= 2; };


// sequential version
for (auto& xx : x) 
  xx *= 2;

// parallel version
parallel_for_each(x, [] (double& xx) { xx *= 2; };
```
The loop functions automatically wait for all jobs to finish, but only when 
called from the main thread. 

### Nested parallel loops

It is possible to nest parallel for loops, provided that we don't need to wait
for inner loops.
```cpp
std::vector<double> x(10, 1);

// sequential version
for (int i = 0; i < x.size(); ++i) {
  for (int j = 4; j < 9; j++) {
    x[i] *= j;
  }
}

// parallel version
parallel_for(0, x.size(), [&] (int i) { 
  // *important*: capture i by copy
  parallel_for(4, 9, [&, i] (int j) {  x[i] *= j; }); // doesn't wait
}; // does wait
```

### Local thread pool

A [`ThreadPool`](https://tnagler.github.io/quickpool/classquickpool_1_1ThreadPool.html) 
can be set up manually, with an arbitrary number of threads. When the pool 
goes out of scope, all threads joined.

```cpp
ThreadPool pool(2); // thread pool with two threads

pool.push([] {});
pool.async([] {});
pool.wait();

pool.parallel_for(2, 5, [&] (int i) {});
auto x = std::vector<double>{10};
pool.parallel_for_each(x, [] (double& xx) {});
```
