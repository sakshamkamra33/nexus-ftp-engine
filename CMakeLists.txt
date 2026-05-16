cmake_minimum_required(VERSION 3.10)
project(ftp_server VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Common Sources ───────────────────────────────────────────────────────────
set(COMMON_SOURCES
    src/common/platform.cpp
)

# ─── Server Sources ───────────────────────────────────────────────────────────
set(SERVER_SOURCES
    src/server/thread_pool.cpp
    src/server/auth.cpp
    src/server\session.cpp
    src/server/admin_server.cpp
    src/server/ftp_server.cpp
    src/server/main.cpp
)

# Replace backslash with forward slash for CMake consistency
string(REPLACE "\\" "/" SERVER_SOURCES "${SERVER_SOURCES}")

# ─── OS Specific Links ────────────────────────────────────────────────────────
set(OS_LIBS "")
if(WIN32)
    set(OS_LIBS ws2_32 mswsock)
else()
    set(OS_LIBS pthread)
endif()

# ─── Main Executable ──────────────────────────────────────────────────────────
add_executable(ftp_server ${COMMON_SOURCES} ${SERVER_SOURCES})
target_include_directories(ftp_server PRIVATE src)
target_link_libraries(ftp_server PRIVATE ${OS_LIBS})

# ─── Tools ────────────────────────────────────────────────────────────────────
add_executable(hash_password tools/hash_password.cpp)
target_include_directories(hash_password PRIVATE src)

# ─── Tests ────────────────────────────────────────────────────────────────────
enable_testing()

# Thread Pool Test
add_executable(test_thread_pool src/server/thread_pool.cpp tests/test_thread_pool.cpp)
target_include_directories(test_thread_pool PRIVATE src)
target_link_libraries(test_thread_pool PRIVATE ${OS_LIBS})
add_test(NAME ThreadPoolTest COMMAND test_thread_pool)

# Security Test
add_executable(test_security tests/test_security.cpp src/server/auth.cpp)
target_include_directories(test_security PRIVATE src)
target_link_libraries(test_security PRIVATE ${OS_LIBS})
add_test(NAME SecurityTest COMMAND test_security)

# Performance Test
add_executable(test_performance ${COMMON_SOURCES} tests/test_performance.cpp)
target_include_directories(test_performance PRIVATE src)
target_link_libraries(test_performance PRIVATE ${OS_LIBS})
add_test(NAME PerformanceTest COMMAND test_performance)

# FileOps Test
add_executable(test_fileops ${COMMON_SOURCES} tests/test_fileops.cpp)
target_include_directories(test_fileops PRIVATE src)
target_link_libraries(test_fileops PRIVATE ${OS_LIBS})
add_test(NAME FileOpsTest COMMAND test_fileops)
