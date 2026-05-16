# NexusFTP - High-Performance C++ Server

![C++](https://img.shields.io/badge/C++-14-blue.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

A production-grade, multi-threaded FTP Server written in modern C++14. This project was completely refactored from a monolithic script into a highly scalable, secure, and performant backend system designed to FAANG-level engineering standards.

## 🚀 Key Architectural Features

### 1. Zero-Copy File Transfers
Bypasses traditional user-space RAM buffers (`fread()` -> `send()`). It uses **OS Kernel-level zero-copy APIs** (`TransmitFile` on Windows, `sendfile` on Linux) to stream data directly from the disk controller to the network socket, achieving maximum theoretical NIC throughput with minimal CPU overhead.

### 2. Custom Threading Primitives
To ensure flawless compilation on legacy toolchains (like MinGW 6.3) that lack `std::thread` support, this project implements its own RAII-compliant concurrency wrappers (`Mutex`, `CondVar`, `Thread`, `AtomicBool`, `AtomicU64`) directly over the native Win32 API.

### 3. Asynchronous Thread Pool
Rather than spawning a new OS thread per connection (which scales poorly), the server utilizes a pre-allocated pool of worker threads synchronized via condition variables, providing `O(1)` connection handoff and preventing thread exhaustion under heavy load.

### 4. Precision Bandwidth Throttling
Features a custom **Token-Bucket Rate Limiter** to enforce granular upload and download speed limits per connection, preventing a single client from saturating network bandwidth.

### 5. Advanced FTP Protocol Support
Fully implements modern FTP extensions including:
* **`REST` (Transfer Resume):** Pause and resume massive file transfers natively.
* **`MDTM` & `SIZE`:** Real-time file metadata for synchronization tools.
* **`PASV` & `PORT`:** Robust handling of passive and active data channels.

---

## 🛡️ Security Hardening

* **Salted SHA-256 Passwords:** Credentials are cryptographically hashed using a custom, dependency-free SHA-256 implementation. Raw passwords are never stored.
* **Jail / Chroot Environment:** Virtualized directory paths prevent malicious clients from executing Directory Traversal (`../..`) attacks to escape the `ftproot`.
* **Brute-Force Protection:** Automatically bans IPs after a threshold of failed login attempts.
* **DDoS & Idle Mitigation:** Enforces `SO_RCVTIMEO` socket timeouts, `MAX_CMD_LENGTH` bounds checking, and hard quotas on maximum file sizes to prevent resource starvation.

---

## 📊 Live Admin Console

The server spawns a secondary local-only listener (default: port `8080`) providing a secure `telnet` interface. Administrators can stream real-time metrics (Active Connections, Bytes Transferred, CPU/Thread states) and gracefully drain connections for maintenance shutdowns without force-killing active file transfers.

---

## 🛠️ Build & Deployment

This project uses `CMake` and is cross-platform capable (Windows/Linux). 

### Prerequisites
* CMake 3.10+
* A C++14 compliant compiler (GCC/MinGW, Clang, or MSVC)

### Local Build (Windows / Linux)
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

### Running the Server
```bash
./ftp_server --config config/server.conf
```

### Docker (Containerized Linux Deployment)
```bash
docker build -t ftp-server:latest .
docker run -p 21:21 -p 8080:8080 -p 50000-50100:50000-50100 ftp-server:latest
```

---

## 📂 Project Structure

```text
├── CMakeLists.txt         # Build configuration
├── Dockerfile             # Multi-stage container deployment
├── config/
│   ├── server.conf        # Network, Tuning, and Security settings
│   └── users.conf         # SHA-256 Hashed user credentials
├── src/
│   ├── common/            # Platform abstraction (Zero-copy, Threading, Crypto)
│   └── server/            # FTP Protocol logic, ThreadPool, Admin console
├── tests/                 # Automated diagnostic suites (Security, Performance)
└── tools/                 # CLI tools (e.g., hash_password.cpp)
```

## 📝 License
This project is open-source and available under the MIT License.
