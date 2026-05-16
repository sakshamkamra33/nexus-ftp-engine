// ============================================================================
// test_thread_pool.cpp  —  Phase 2 Test Suite
// Tests: basic execution, concurrency, shutdown, exception safety, metrics
// No std::atomic or std::thread needed (uses our custom primitives).
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef min
#undef max

#include "server/thread_pool.h"
#include "common/logger.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

// ─── Tiny test framework ─────────────────────────────────────────────────────
static int g_passed = 0;
static int g_failed = 0;

void check(bool cond, const char* name) {
    if (cond) {
        printf("  [PASS] %s\n", name);
        g_passed++;
    } else {
        printf("  [FAIL] %s\n", name);
        g_failed++;
    }
    fflush(stdout);
}

// ─── Shared counter (mutex-protected, no std::atomic needed) ─────────────────
struct Counter {
    ftp::threading::Mutex m;
    int val = 0;
    void inc()       { ftp::threading::LockGuard lk(m); val++; }
    int  get() const { return val; }
};

// ─── Tests ───────────────────────────────────────────────────────────────────

void test_create_destroy() {
    printf("\nTest: Create and destroy (no tasks)\n");
    {
        ftp::ThreadPool pool(4);
        check(pool.workerCount() == 4, "workerCount == 4");
        check(pool.isRunning(),        "isRunning after create");
        pool.shutdown();
        check(!pool.isRunning(),       "not running after shutdown");
    }
    check(true, "pool destroyed cleanly");
}

void test_basic_execution() {
    printf("\nTest: Basic task execution (50 tasks, 4 workers)\n");
    Counter c;
    {
        ftp::ThreadPool pool(4);
        for (int i = 0; i < 50; i++) {
            pool.submit([&c]() { c.inc(); });
        }
        pool.shutdown();
    }
    check(c.get() == 50, "all 50 tasks executed");
}

void test_single_worker() {
    printf("\nTest: Single worker (100 tasks)\n");
    Counter c;
    {
        ftp::ThreadPool pool(1);
        for (int i = 0; i < 100; i++) {
            pool.submit([&c]() { c.inc(); });
        }
        pool.shutdown();
    }
    check(c.get() == 100, "all 100 tasks executed by 1 worker");
}

void test_many_workers() {
    printf("\nTest: Many workers (8 workers, 200 tasks)\n");
    Counter c;
    {
        ftp::ThreadPool pool(8);
        for (int i = 0; i < 200; i++) {
            pool.submit([&c]() {
                c.inc();
                Sleep(1); // small delay to stress concurrency
            });
        }
        pool.shutdown();
    }
    check(c.get() == 200, "all 200 tasks executed by 8 workers");
}

void test_exception_safety() {
    printf("\nTest: Exception safety\n");
    Counter c;
    {
        ftp::ThreadPool pool(2);
        // Submit a task that throws
        pool.submit([]() { throw 42; });
        // Submit 10 normal tasks — they must still run
        for (int i = 0; i < 10; i++) {
            pool.submit([&c]() { c.inc(); });
        }
        pool.shutdown();
    }
    check(c.get() == 10, "10 tasks ran after exception task");
}

void test_reject_after_shutdown() {
    printf("\nTest: Reject tasks after shutdown\n");
    Counter c;
    {
        ftp::ThreadPool pool(2);
        pool.shutdown();
        // These should be silently rejected
        pool.submit([&c]() { c.inc(); });
        pool.submit([&c]() { c.inc(); });
    }
    check(c.get() == 0, "rejected tasks did not execute");
}

void test_metrics() {
    printf("\nTest: Metrics (workerCount, isRunning)\n");
    ftp::ThreadPool pool(3);
    check(pool.workerCount() == 3, "workerCount == 3");
    check(pool.isRunning(),        "isRunning == true before shutdown");
    pool.shutdown();
    check(!pool.isRunning(),       "isRunning == false after shutdown");
}

void test_sequential_ordering() {
    printf("\nTest: Sequential ordering (1 worker)\n");
    // With 1 worker tasks must execute in submit order
    std::vector<int> order;
    ftp::threading::Mutex m;
    {
        ftp::ThreadPool pool(1);
        for (int i = 0; i < 5; i++) {
            int id = i;
            pool.submit([&order, &m, id]() {
                ftp::threading::LockGuard lk(m);
                order.push_back(id);
            });
        }
        pool.shutdown();
    }
    bool ordered = true;
    for (int i = 0; i < (int)order.size(); i++) {
        if (order[i] != i) { ordered = false; break; }
    }
    check(order.size() == 5, "all 5 ordered tasks ran");
    check(ordered,           "tasks ran in FIFO order");
}

// ─── Main ────────────────────────────────────────────────────────────────────
int main() {
    // Suppress thread pool logs during tests
    ftp::Logger::instance().setLevel(ftp::LogLevel::LVL_FATAL);

    printf("============================================\n");
    printf("  Phase 2 Test Suite: Thread Pool\n");
    printf("============================================\n");

    test_create_destroy();
    test_basic_execution();
    test_single_worker();
    test_many_workers();
    test_exception_safety();
    test_reject_after_shutdown();
    test_metrics();
    test_sequential_ordering();

    printf("\n============================================\n");
    printf("  Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("============================================\n");

    return (g_failed > 0) ? 1 : 0;
}
