// ============================================================================
// thread_pool.cpp — Thread pool with correct mutex-protected condvar usage
// ============================================================================
#include "thread_pool.h"
#include "../common/logger.h"
#include <sstream>

namespace ftp {

ThreadPool::ThreadPool(size_t numWorkers) : shutdown_(false), activeTasks_(0) {
    std::ostringstream ss;
    ss << "Starting thread pool with " << numWorkers << " workers";
    LOG_INFO("ThreadPool", ss.str());

    workers_.reserve(numWorkers); // CRITICAL: prevent reallocation that invalidates 'this' in lambdas
    for (size_t i = 0; i < numWorkers; ++i) {
        workers_.emplace_back(threading::Thread([this]() { workerLoop(); }));
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::submit(std::function<void()> task) {
    mutex_.lock();
    if (shutdown_.load()) {
        mutex_.unlock();
        LOG_WARN("ThreadPool", "Task rejected - pool shutting down");
        return;
    }
    tasks_.push(std::move(task));
    cv_.notify_one();  // notify_one called UNDER the lock
    mutex_.unlock();
}

void ThreadPool::shutdown() {
    if (shutdown_.exchange(true)) return; // already shutdown

    // Signal all workers under lock so notify_all sees correct waiters_ count
    mutex_.lock();
    LOG_INFO("ThreadPool", "Shutting down - signalling workers...");
    cv_.notify_all();  // called UNDER the lock
    mutex_.unlock();

    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    LOG_INFO("ThreadPool", "All workers joined.");
}

size_t ThreadPool::activeCount() const {
    return (size_t)activeTasks_.load();
}

size_t ThreadPool::pendingCount() const {
    threading::LockGuard lock(mutex_);
    return tasks_.size();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;

        mutex_.lock();

        // Wait while no work and not shutting down
        while (!shutdown_.load() && tasks_.empty()) {
            cv_.wait(mutex_); // releases mutex, blocks on sema, re-acquires
        }

        // Shutdown with no tasks left -> exit
        if (shutdown_.load() && tasks_.empty()) {
            mutex_.unlock();
            return;
        }

        task = std::move(tasks_.front());
        tasks_.pop();
        mutex_.unlock();

        activeTasks_.inc();
        try { task(); } catch (...) {
            LOG_ERROR("ThreadPool", "Task threw an exception");
        }
        activeTasks_.dec();
    }
}

} // namespace ftp
