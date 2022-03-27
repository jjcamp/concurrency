# Concurrency Library

This library is a collection of concurrency primitives built using futex-like
APIs as well as high-level constructs built on top of those primitives.

**latch:** An implementation of C++20's `latch`

**counting_semaphore:** And implementation of C++20's `counting_semaphore`

**binary_semaphore:** A specialization of `counting_semaphore`

**mutex:** A lightweight mutex that adapts a `binary_semaphore` to the
_TimedMutex_ interface

**event:** A mechanism for signaling state changes

**mpsc::channel:** Based on Rust's `Channel` interface, but can be either
unbounded (fully asynchronous) or bounded.

The library targets C++17, but porting to C++14 is trivial if desired.