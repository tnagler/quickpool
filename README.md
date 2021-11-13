# tpool

> An easy-to-use, header-only work stealing thread pool in C++11

## Why tpool?

* **no install**: Single-header library, just drop in your project and enjoy.

* **portable**: Only requires a [C++11 compliant](https://en.cppreference.com/w/cpp/compiler_support) compiler.

* **fast**: Uses a [work stealing](https://en.wikipedia.org/wiki/Work_stealing) queue for distributing tasks with [lock-free](https://en.wikipedia.org/wiki/Non-blocking_algorithm#Lock-freedom) pops.

* **user-friendly**: Dead simple API, including direct access to a global pool for maximal convenience.

## Usage

Basic usage is demonstrated below. See the [API documentation](https://tnagler.github.io/tpool/) for more details.

### Static access to a global pool

The easiest way is to use the static `push()` , [`async()`](https://tnagler.github.io/tpool/namespacetpool.html#a10575809d24ead3716e312585f90a94a) , and `wait()` functions. They give access to a global thread pool that is only instantiated once with as many threads as there are cores.

```cpp
#include "tpool.hpp"

tpool::push([] { /* some work */ });
tpool::push([] { /* some work */ });
tpool::wait(); // waits for all current jobs to finish
```

If a task also returns a result, use `async()` , which returns an [ `std::future` ](https://en.cppreference.com/w/cpp/thread/future) for the result.

```cpp
auto f = tpool::async([] { return 1 + 1; });
// do something else ...
auto result = f.get();  // waits until done and returns result
```

Both `push()` and `async()` can also be called with extra arguments passed to the function.

```cpp
auto work = [] (std::string title, int i) { 
  std::cout << title << ": " << i << std::endl; 
};
tpool::push(work, "first title", 5);
tpool::async(work, "other title", 99);
tpool::wait();
// prints: --------
// first title: 5
// other title: 99
```

### Local thread pool

Thread pols can also be set up manually, with an arbitrary number of threads. When the pool goes out of scope, all threads joined.

```cpp
{
  tpool::ThreadPool pool(2);  // thread pool with two threads
  pool.push([] { /* some work */ });
  pool.async([] { /* some work */ });
  pool.wait(); // waits for all current jobs to finish
}
// threads are joined
```

### Task synchronization

In general, the pool may process the tasks in any order. Synchronization between tasks (e.g., one thread waiting intermediate results) must be done manually. Standard tools are [ `std::mutex` ](https://en.cppreference.com/w/cpp/thread/mutex) and [ `std::condition_variable` ](https://en.cppreference.com/w/cpp/thread/condition_variable). `tpool` exposes another synchronization primitive, `FinishLine` , that may be useful.

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

If the number of runners is not known up front, you can start a runner on the fly using `FinishLine::start()` .
