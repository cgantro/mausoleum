#include "ConcurrentQueue.h"

template <typename T>
void ConcurrentQueue<T>::push(T item) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return;
        if (capacity_ && queue_.size() >= capacity_) { queue_.pop(); ++dropped_; }
        queue_.push(std::move(item));
    }
    cv_.notify_one();
}

template <typename T>
bool ConcurrentQueue<T>::pop(T& out) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
        return false;
    }
    out = std::move(queue_.front());
    queue_.pop();
    return true;
}

template <typename T>
void ConcurrentQueue<T>::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
}
