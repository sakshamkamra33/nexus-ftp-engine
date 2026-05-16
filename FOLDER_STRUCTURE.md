# Project Folder & File Structure Breakdown
**Project Name:** NexusFTP - High-Performance C++ File Server

*This document explains the architecture of the codebase using the "Separation of Concerns" design pattern. It details what every folder and file is responsible for.*

---

## 1. The Root Directory (DevOps & Build)
* **`CMakeLists.txt`**: The master build script. It tells the C++ compiler exactly how to link the files together, allowing the project to be built on Windows, Linux, or Mac without modifying the source code.
* **`Dockerfile` & `.dockerignore`**: The containerization scripts. These allow the entire server to be packaged into an isolated Linux environment and deployed instantly to cloud providers like AWS or GCP.
* **`PROJECT_EXPLANATION.md` / `README.md`**: Core documentation files detailing the technical achievements.

---

## 2. The `config/` Folder (Configuration)
* **`server.conf`**: The main brain of the server's parameters. Controls the port, root directory, bandwidth limits, timeout thresholds, and max connections.
* **`users.conf`**: The database for user authentication. It stores usernames and their highly secure SHA-256 password hashes.

---

## 3. The `src/common/` Folder (Low-Level Abstractions)
This folder contains code that has *nothing* to do with the FTP protocol. It is pure utility code that could be copy-pasted into any other C++ project (like an HTTP server or a game engine).
* **`platform.h` / `platform.cpp`**: The most important file for cross-platform compatibility. It hides all the messy Windows-specific (`winsock2.h`) and Linux-specific (`sys/socket.h`) networking code. It also holds the advanced `TransmitFile` zero-copy functionality.
* **`win32_threads.h`**: The custom concurrency library. Since legacy MinGW lacked `std::thread`, this file manually wraps Win32 OS APIs to create safe `Mutex`, `CondVar`, and `Thread` objects.
* **`sha256.h`**: The pure C++ cryptographic hashing algorithm used to secure passwords.
* **`logger.h`**: The centralized logging system that prints timestamps and cleanly formats errors/warnings to the console.
* **`config.h`**: A simple parser that reads `server.conf` and turns it into C++ variables.

---

## 4. The `src/server/` Folder (The Business Logic)
This is where the actual FTP Server lives. It relies heavily on the tools built inside `src/common/`.
* **`main.cpp`**: The entry point. It simply loads the config and starts the `FTPServer`.
* **`ftp_server.h` / `.cpp`**: The master server class. It listens on port 21, accepts incoming clients, and tosses them into the Thread Pool.
* **`thread_pool.h` / `.cpp`**: The asynchronous worker engine. It creates a fixed number of threads that sleep until a client connects, making the server incredibly fast and memory-efficient.
* **`session.h` / `.cpp`**: The core FTP protocol engine. When a user sends a command like `RETR` or `CWD`, this file parses the text, checks security permissions, and decides what to do.
* **`data_channel.h` / `.cpp`**: FTP uses *two* ports. Port 21 is for commands, and a random secondary port is for data. This file handles the complex math of Active (`PORT`) and Passive (`PASV`) data connections.
* **`transfer.h`**: The performance engine. It contains the Token-Bucket Rate Limiter and the zero-copy routing logic for uploading and downloading files.
* **`auth.h` / `.cpp`**: The security gatekeeper. It checks passwords against `users.conf` and bans IP addresses if they fail too many times.
* **`admin_server.h` / `.cpp`**: The secondary server running on port `8080` that allows an admin to type `STATS` or `STOP` to monitor and safely shut down the system.

---

## 5. The Output & Testing Folders
* **`tests/`**: Contains small C++ scripts (`test_security.cpp`, `test_performance.cpp`, etc.). These are automated diagnostics to prove thread pools and cryptography work flawlessly without having to launch the whole server.
* **`tools/`**: Contains utility scripts, primarily `hash_password.cpp`. This is a CLI tool used to generate SHA-256 hashes when adding new users to the configuration.
* **`build/`**: The trash bin for compiled code. When running `g++` or `cmake`, all `.exe` executables and intermediate binaries go here to keep the main source code clean.
* **`ftproot/`**: The "sandbox". This is the actual virtual hard drive of the FTP server. Clients are physically restricted from escaping this folder.

---

## 💡 The "Elevator Pitch" for Interviews
If someone asks you for a 30-second summary of your architecture, you should say: 

> *"The project is divided into three layers. The **Common Layer** handles OS-level networking, custom threading, and cryptography. The **Server Layer** implements the FTP protocol, active/passive data channels, and the thread pool. Finally, the **Testing & Configuration Layer** handles CI/CD diagnostics and deployment parameters. This Separation of Concerns ensures the core FTP logic is completely decoupled from the underlying operating system."*
