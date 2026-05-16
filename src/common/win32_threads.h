// ============================================================================
// win32_threads.h — MinGW 6.3 / POSIX compatible threading
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
  #include <functional>
  #include <stdexcept>
  #include <cstdint>

  namespace ftp {
  namespace threading {

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

  class LockGuard {
  public:
      explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
      ~LockGuard() { m_.unlock(); }
      LockGuard(const LockGuard&) = delete;
  private:
      Mutex& m_;
  };

  class CondVar {
  public:
      CondVar() : waiters_(0) {
          sema_ = CreateSemaphore(nullptr, 0, 0x7FFFFFFF, nullptr);
          if (!sema_) throw std::runtime_error("CreateSemaphore failed");
      }
      ~CondVar() { CloseHandle(sema_); }
      void wait(Mutex& m) {
          waiters_++;
          m.unlock();
          WaitForSingleObject(sema_, INFINITE);
          m.lock();
          waiters_--;
      }
      void notify_one() { if (waiters_ > 0) ReleaseSemaphore(sema_, 1, nullptr); }
      void notify_all() { if (waiters_ > 0) ReleaseSemaphore(sema_, (LONG)waiters_, nullptr); }
      CondVar(const CondVar&) = delete;
      CondVar& operator=(const CondVar&) = delete;
  private:
      HANDLE sema_;
      LONG   waiters_;
  };

  class Thread {
  public:
      Thread() = default;
      explicit Thread(std::function<void()> fn) : handle_(nullptr) {
          fn_ = new std::function<void()>(std::move(fn));
          handle_ = CreateThread(nullptr, 0, threadFunc, fn_, 0, nullptr);
          if (!handle_) { delete fn_; fn_ = nullptr; throw std::runtime_error("CreateThread failed"); }
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
          std::function<void()>* fn = static_cast<std::function<void()>*>(p);
          (*fn)();
          delete fn;
          return 0;
      }
      std::function<void()>* fn_     = nullptr;
      HANDLE                 handle_ = nullptr;
  };

  class AtomicBool {
  public:
      explicit AtomicBool(bool v = false) { val_ = v ? 1L : 0L; }
      bool load()    const  { return val_ != 0; }
      void store(bool v)    { InterlockedExchange(&val_, v ? 1L : 0L); }
      bool exchange(bool v) { return InterlockedExchange(&val_, v ? 1L : 0L) != 0; }
  private:
      volatile LONG val_;
  };

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

#else

  #include <thread>
  #include <mutex>
  #include <condition_variable>
  #include <atomic>
  #include <functional>
  #include <cstdint>

  namespace ftp {
  namespace threading {

  class Mutex {
  public:
      void lock() { m_.lock(); }
      void unlock() { m_.unlock(); }
      std::mutex& native_handle() { return m_; }
  private:
      std::mutex m_;
  };

  class LockGuard {
  public:
      explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
      ~LockGuard() { m_.unlock(); }
  private:
      Mutex& m_;
  };

  class CondVar {
  public:
      void wait(Mutex& m) {
          std::unique_lock<std::mutex> lk(m.native_handle(), std::adopt_lock);
          cv_.wait(lk);
          lk.release();
      }
      void notify_one() { cv_.notify_one(); }
      void notify_all() { cv_.notify_all(); }
  private:
      std::condition_variable cv_;
  };

  class Thread {
  public:
      Thread() = default;
      explicit Thread(std::function<void()> fn) : t_(std::move(fn)) {}
      ~Thread() { if (joinable()) join(); }
      void join() { t_.join(); }
      bool joinable() const { return t_.joinable(); }
      Thread(Thread&& o) noexcept = default;
      Thread& operator=(Thread&& o) noexcept = default;
  private:
      std::thread t_;
  };

  class AtomicBool {
  public:
      explicit AtomicBool(bool v = false) : val_(v) {}
      bool load() const { return val_.load(); }
      void store(bool v) { val_.store(v); }
      bool exchange(bool v) { return val_.exchange(v); }
  private:
      std::atomic<bool> val_;
  };

  class AtomicU64 {
  public:
      AtomicU64() : val_(0) {}
      explicit AtomicU64(uint64_t v) : val_(v) {}
      uint64_t load() const { return val_.load(); }
      void store(uint64_t v) { val_.store(v); }
      void add(uint64_t v) { val_.fetch_add(v); }
      void inc() { val_.fetch_add(1); }
      void dec() {
          uint64_t current = val_.load();
          while (current > 0 && !val_.compare_exchange_weak(current, current - 1)) {}
      }
  private:
      std::atomic<uint64_t> val_;
  };

  } // namespace threading
  } // namespace ftp

#endif
