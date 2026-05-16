# How to Run and Manage the FTP Server

*This is your quick-start guide for compiling, running, and managing your high-performance C++ FTP Server locally.*

---

## Step 1: Open Your Terminal
Open PowerShell or your VS Code terminal and ensure you are inside the main project folder:
```powershell
cd C:\Users\ASUS\CODING\PROJECTS\ftp_project
```

---

## Step 2: Compile the Server
Whenever you make a change to the C++ code, you must recompile it into an executable (`.exe`). Run this exact command to compile the server:
```powershell
g++ -std=c++14 -O2 -Isrc -o build\ftp_server.exe src\common\platform.cpp src\server\thread_pool.cpp src\server\auth.cpp src\server\session.cpp src\server\admin_server.cpp src\server\ftp_server.cpp src\server\main.cpp -lws2_32 -lmswsock
```
*(If it succeeds, it will output nothing. The compiled server is now saved at `build\ftp_server.exe`)*

---

## Step 3: Start the Server
Run the executable to start hosting:
```powershell
.\build\ftp_server.exe
```
You will see the startup logs, and it will say **"Ready. Press Ctrl+C to stop."** Keep this terminal window open.

---

## Step 4: Connect to Your FTP Server
Now that the server is running, you can connect to it just like any normal user would. 

### Option A: Using an FTP Client (Recommended)
1. Download and open a free tool like **FileZilla** or **WinSCP**.
2. **Host:** `127.0.0.1` (or `localhost`)
3. **Username:** `admin` *(this is set in your config/users.conf)*
4. **Password:** `password`
5. **Port:** `21`
6. Click **Connect**. You will see the contents of your `ftproot` folder and can start dragging and dropping files!

### Option B: Using Windows File Explorer
1. Open normal Windows File Explorer.
2. In the top address bar, type: `ftp://127.0.0.1` and press Enter.
3. A login prompt will appear. Enter `admin` and `password`.

---

## Step 5: Test the Admin Console
While your server is running in the first terminal, open a **second terminal window** (leave the server running in the background). 
You can run the admin test script we built to see live stats or shut the server down:
```powershell
.\build\test_admin.exe
```
This will connect to port 8080, print your live stats (active connections, bytes transferred), and send the `STOP` command to gracefully shut the server down.

---

## Step 6: Managing Users (Creating New Passwords)
If you ever want to add a new user or change a password, you cannot just type the plain text password into `config/users.conf` because the server only accepts **SHA-256 Hashes**.

To generate a new secure hash, compile and run your tool:
```powershell
g++ -Isrc -o build\hash_password.exe tools\hash_password.cpp
.\build\hash_password.exe
```
It will ask you to type a password, and it will output a secure hash. You then copy that hash and paste it into `config/users.conf` following this format:
```text
my_new_user: <pasted_hash>
```
