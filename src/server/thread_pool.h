// ============================================================================
// thread_pool.h — Fixed-size thread pool (Win32/POSIX compatible)
// Uses custom threading wrappers that work on all MinGW versions.
// ============================================================================
#pragma once

#include "../common/win32_threads.h"
#include <vector>
#include <queue>
#include <functional>

namespace ftp {

class ThreadPool {
public:
    explicit ThreadPool(size_t numWorkers);
    ~ThreadPool();

    void submit(std::function<void()> task);
    void shutdown();

    size_t activeCount()  const;
    size_t pendingCount() const;
    size_t workerCount()  const { return workers_.size(); }
    bool   isRunning()    const { return !shutdown_.load(); }

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    void workerLoop();

    std::vector<threading::Thread>              workers_;
    std::queue<std::function<void()>>           tasks_;

    mutable threading::Mutex                    mutex_;
    threading::CondVar                          cv_;
    threading::AtomicBool                       shutdown_;
    threading::AtomicU64                        activeTasks_;
};

} // namespace ftp
