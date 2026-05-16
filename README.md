# NexusFTP Engine 🚀

NexusFTP is a high-performance, low-level FTP server built entirely from scratch in C++14. It is designed to demonstrate deep understanding of Systems Programming, multithreading, raw TCP/IP socket manipulation, and cross-platform architecture (Win32 & POSIX). 

It is currently containerized via Docker and deployed to an AWS EC2 Ubuntu instance.

## 🌟 Live Demo
The FTP server features a custom-built, embedded HTTP engine that serves a real-time React-style telemetry dashboard using vanilla HTML/CSS and CSS Grid!

👉 **View the Live Dashboard:** [http://204.236.201.82:8080](http://204.236.201.82:8080)

### 🔌 How to Test the Backend Connection
You can connect to the raw C++ FTP socket using your computer's built-in terminal or any FTP client (like FileZilla). When you connect, you will instantly see the live dashboard telemetry update!

1. Open your terminal or command prompt.
2. Type the following connection command:
   ```bash
   ftp 204.236.201.82
   ```
3. Enter the following demo credentials when prompted:
   - **Username:** `guest`
   - **Password:** `guest`
4. Try typing `ls` to request a directory listing, then look at your dashboard to see the active socket count increase!

## ⚙️ Core Technical Features
* **Raw Socket API:** Handled network communication natively in C++ without high-level networking libraries.
* **Custom Concurrency:** Built a robust Thread Pool using `std::mutex` and `std::condition_variable` to manage hundreds of concurrent client connections.
* **Zero-Copy Architecture:** Integrated OS-level optimization hooks like `sendfile()` (Linux) and `TransmitFile()` (Windows) for maximum bandwidth throughput.
* **Cloud & DevOps:** Engineered a multi-stage Dockerfile to compile natively on Linux, resulting in a minimal memory footprint, deployed via AWS EC2.
* **Security Subsystem:** Includes SHA-256 hashed password authentication, environment variable secret injection, and anti-brute-force connection rejection.
