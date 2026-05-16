# Project Architecture & Refactoring Breakdown
**Project Name:** NexusFTP - High-Performance C++ File Server

*This document serves as a comprehensive reference guide for interviews, explaining the evolution of the project from a basic script to a production-grade backend system.*

---

## 1. What We Started With (The Original Code)
The original project was a basic, functional C++ FTP Server script. However, it had several critical architectural flaws that made it unfit for production environments:
* **The Monolith:** Everything was jammed into a single file (`ftp_server.cpp`) containing hundreds of lines of mixed logic.
* **Compiler Crashes:** It relied on modern `std::thread`, which famously causes compilation errors and crashes on older Windows/MinGW toolchains.
* **Zero Security:** Passwords were saved in plain text. Any malicious client could connect, spam commands to crash the server, or use path traversal (`cd ../../../`) to access private files on the hard drive outside the FTP folder.
* **Poor Performance:** File transfers used standard `fread()` memory buffers, which burned CPU cycles and RAM. The server would block (freeze) completely if too many users connected.

---

## 2. What We Transformed It Into (The Final Architecture)
Over the course of 8 Phases, the monolith was systematically dismantled and rebuilt from the ground up using **FAANG-level systems engineering principles**. It was transformed into a distributed, multi-threaded, high-performance server.

### 🟢 Core Architecture & Threading (The Foundation)
Because the compiler didn't support `std::thread`, custom operating system wrappers were built.
* **Custom Synchronization Primitives:** Custom `Mutex`, `CondVar`, and `AtomicBool` classes were built directly on top of the native Windows API (`CreateThread`, `InterlockedExchange`). This demonstrates a deep understanding of how operating systems manage memory and CPU context-switching.
* **Asynchronous Thread Pool:** Instead of launching a new thread for every user (which wastes RAM), a **Thread Pool** was built with a fixed number of worker threads (e.g., 8). When a client connects, the connection is instantly handed off to an idle worker via a condition variable. This gives the server highly scalable $O(1)$ connection handoff.

### ⚡ Performance Engine (The Speed)
File transfer mechanisms were completely rewritten to maximize network throughput.
* **Kernel-Level Zero-Copy Transfers:** The old buffer loops were replaced with the native OS `TransmitFile` API. This allows the file to move directly from the hard drive (disk controller) straight to the Network Interface Card (NIC) inside the OS kernel. It bypasses user-space RAM entirely, dropping CPU usage to near 0% even under heavy load.
* **Socket Tuning:** `SO_SNDBUF` (256KB) and `SO_RCVBUF` (64KB) limits were artificially increased at the TCP level to allow massive packets to flow through Local Area Networks (LAN) without bottlenecking.
* **Token-Bucket Rate Limiter:** A precise algorithm was built that allows for granular bandwidth limiting per user (e.g., max 100 KB/s). This ensures one user downloading a massive file doesn't lag the entire server.

### 🛡️ Security Hardening (The Defense)
The server was secured against the most common network attacks:
* **Dependency-Free SHA-256:** A pure C++ cryptographic hashing algorithm was implemented. The server reads `users.conf`, takes the incoming password, hashes it using SHA-256 with a cryptographic salt, and compares it to the saved hash. Plain-text passwords are gone.
* **Brute-Force Mitigation:** If an IP address fails to log in multiple times, the server automatically bans them to stop dictionary attacks.
* **The "Chroot Jail":** Strict path validation was implemented. If a user tries to type `RETR ../../../../Windows/System32/secret.txt`, the server intercepts the path, normalizes it, and strictly restricts them to the isolated `ftproot` folder.
* **Socket Timeouts & Sanitization:** If a client connects but goes idle (trying to hold the socket hostage), the `SO_RCVTIMEO` timer automatically terminates the connection. All incoming commands are also limited to 512 bytes to prevent Buffer Overflow attacks.

### 📡 FTP Protocol Advancements
The implementation supports modern FTP client extensions (like FileZilla).
* **`REST` (Transfer Resume):** If a user is downloading a large file and the connection drops, they don't have to restart. The `REST` command allows the server to skip directly to the exact byte-offset on disk and resume the stream.
* **`MDTM` & `SIZE`:** Added support for file metadata so clients can check modification timestamps and exact file sizes for fast directory syncing.
* **Recursive Deletion:** Implemented advanced backend file operations to recursively wipe directories and their contents.

### 🎮 The Live Admin Console
A secondary "control plane" was added to the application.
* The server silently spawns a listener on port `8080` bound strictly to `127.0.0.1` (Localhost).
* Administrators can connect to this port and type `STATS` to see a live dashboard of active connections, total bytes transferred, and blocked login attempts.
* Typing `STOP` initiates a **Graceful Shutdown**: The server stops accepting new users, waits for current file transfers to finish safely, cleans up all memory and thread locks, and exits smoothly.

---

## 3. Why this is a FAANG-Level Project
This project demonstrates highly sought-after engineering skills:
* **TCP/IP Networking:** Managing sockets, passive/active data channels, and packet transmission.
* **OS Kernel APIs:** Understanding user-space vs. kernel-space operations (`TransmitFile` Zero-copy).
* **Concurrent Programming:** Safely sharing memory using Mutexes, Thread Pools, and Condition Variables without deadlocks.
* **Cybersecurity:** Cryptographic hashing (SHA-256) and defense-in-depth sanitization.
* **DevOps:** Standardizing builds with CMake and containerizing environments with Docker.
