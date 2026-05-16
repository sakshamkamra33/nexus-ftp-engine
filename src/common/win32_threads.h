// ============================================================================
// win32_threads.h — MinGW 6.3 compatible threading (XP+ Win32 API only)
// CondVar uses a single semaphore; notify_all posts N times for N waiters.
// ============================================================================
#pragma once

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #undef min
  #undef max
#else
  #include <pthread.h>
#endif

#include <functional>
#include <stdexcept>
#include <cstdint>

namespace ftp {
namespace threading {

// ─── Mutex (CRITICAL_SECTION) ────────────────────────────────────────────────
class Mutex {
public:
    Mutex()  { InitializeCriticalSection(&cs_); }
    ~Mutex() { DeleteCriticalSection(&cs_); }
    void lock()   { EnterCriticalSection(&cs_); }
    void unlock() { LeaveCriticalSection(&cs_); }

    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;

private:
    CRITICAL_SECTION cs_;
};

// ─── LockGuard ───────────────────────────────────────────────────────────────
class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
    LockGuard(const LockGuard&) = delete;
private:
    Mutex& m_;
};

// ─── ConditionVariable (semaphore-based, provably correct) ───────────────────
// notify_all releases the semaphore exactly N times (one per current waiter).
// waiters_ is protected by the caller's mutex (it's incremented/read under lock).
class CondVar {
public:
    CondVar() : waiters_(0) {
        sema_ = CreateSemaphore(nullptr, 0, 0x7FFFFFFF, nullptr);
        if (!sema_) throw std::runtime_error("CreateSemaphore failed");
    }
    ~CondVar() {
        CloseHandle(sema_);
    }

    // MUST be called with mutex LOCKED.
    // Increments waiters_ (under caller's lock), releases mutex, blocks on sema.
    // On wake: re-acquires mutex, decrements waiters_.
    void wait(Mutex& m) {
        waiters_++;     // safe: caller holds the mutex
        m.unlock();
        WaitForSingleObject(sema_, INFINITE);
        m.lock();
        waiters_--;
    }

    // notify_one: release 1 semaphore slot if anyone is waiting.
    // MUST be called with mutex LOCKED (so waiters_ read is safe).
    void notify_one() {
        if (waiters_ > 0) {
            ReleaseSemaphore(sema_, 1, nullptr);
        }
    }

    // notify_all: release N semaphore slots for N current waiters.
    // MUST be called with mutex LOCKED.
    void notify_all() {
        if (waiters_ > 0) {
            ReleaseSemaphore(sema_, (LONG)waiters_, nullptr);
        }
    }

    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;

private:
    HANDLE sema_;
    LONG   waiters_; // guarded by caller's mutex
};

// ─── Thread (Win32 CreateThread) ─────────────────────────────────────────────
class Thread {
public:
    Thread() = default;

    explicit Thread(std::function<void()> fn) : handle_(nullptr) {
        // Heap-allocate fn so its address stays valid even if Thread is moved
        fn_ = new std::function<void()>(std::move(fn));
        handle_ = CreateThread(nullptr, 0, threadFunc, fn_, 0, nullptr);
        if (!handle_) {
            delete fn_;
            fn_ = nullptr;
            throw std::runtime_error("CreateThread failed");
        }
    }

    ~Thread() { if (joinable()) join(); }

    void join() {
        if (handle_) {
            WaitForSingleObject(handle_, INFINITE);
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    bool joinable() const { return handle_ != nullptr; }

    Thread(Thread&& o) noexcept : fn_(o.fn_), handle_(o.handle_) {
        o.fn_    = nullptr;
        o.handle_ = nullptr;
    }
    Thread& operator=(Thread&& o) noexcept {
        if (joinable()) join();
        fn_    = o.fn_;    o.fn_    = nullptr;
        handle_ = o.handle_; o.handle_ = nullptr;
        return *this;
    }
    Thread(const Thread&)            = delete;
    Thread& operator=(const Thread&) = delete;

private:
    static DWORD WINAPI threadFunc(LPVOID p) {
        // fn_ was heap-allocated; delete after calling
        std::function<void()>* fn = static_cast<std::function<void()>*>(p);
        (*fn)();
        delete fn;
        return 0;
    }
    std::function<void()>* fn_     = nullptr;
    HANDLE                 handle_ = nullptr;
};

// ─── AtomicBool (InterlockedExchange) ────────────────────────────────────────
class AtomicBool {
public:
    explicit AtomicBool(bool v = false) { val_ = v ? 1L : 0L; }

    bool load()    const  { return val_ != 0; }
    void store(bool v)    { InterlockedExchange(&val_, v ? 1L : 0L); }
    bool exchange(bool v) { return InterlockedExchange(&val_, v ? 1L : 0L) != 0; }

private:
    volatile LONG val_;
};

// ─── AtomicU64 (mutex-protected) ─────────────────────────────────────────────
class AtomicU64 {
public:
    AtomicU64()                : val_(0) {}
    explicit AtomicU64(uint64_t v) : val_(v) {}

    uint64_t load()   const { LockGuard lk(const_cast<Mutex&>(m_)); return val_; }
    void  store(uint64_t v) { LockGuard lk(m_); val_ = v; }
    void  add(uint64_t v)   { LockGuard lk(m_); val_ += v; }
    void  inc()             { LockGuard lk(m_); ++val_; }
    void  dec()             { LockGuard lk(m_); if (val_) --val_; }

private:
    mutable Mutex m_;
    uint64_t      val_;
};

} // namespace threading
} // namespace ftp
