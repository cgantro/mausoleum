#pragma once
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>

template <typename T>
class ConcurrentQueue {
public:
    explicit ConcurrentQueue(size_t capacity = 0) : capacity_(capacity) {}
    size_t size() const { std::lock_guard<std::mutex> lock(mutex_); return queue_.size(); }
    size_t dropped() const { std::lock_guard<std::mutex> lock(mutex_); return dropped_; }
    void push(T item);
    bool pop(T& out);
    void close();

private:
    mutable std::mutex mutex_;
    size_t capacity_;
    size_t dropped_ = 0;
    std::condition_variable cv_;
    std::queue<T> queue_;
    bool closed_ = false;
};

#include "common/ConcurrentQueue.inl"
