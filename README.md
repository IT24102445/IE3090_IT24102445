# IE3090 RemoteOps – Multi-threaded Network Operations & Remote Management System

## 📋 Student & Assignment Information

| Field | Detail |
| :--- | :--- |
| **Course Module** | IE3090 – Network Programming |
| **Student Name** | Rashmika Hapuarachchi |
| **Registration Number** | `IT24102445` |
| **Academic Year** | 2026 |

---

## ⚙️ Personalized System Parameters

| Parameter | Configuration Value |
| :--- | :--- |
| **Agent TCP Listening Port** | `9410` |
| **Session Identification Tag (SID)** | `SID:5442` |
| **Authentication Token** | `OPS-2445` |
| **Source Implementation Files** | [`agent_445.c`](file:///c:/Users/ginura/OneDrive/Documents/rashmika/IE3090_IT24102445/agent_445.c), [`controller_445.c`](file:///c:/Users/ginura/OneDrive/Documents/rashmika/IE3090_IT24102445/controller_445.c), [`Makefile_445`](file:///c:/Users/ginura/OneDrive/Documents/rashmika/IE3090_IT24102445/Makefile_445) |
| **Log File Path** | `remoteops_IT24102445.log` |
| **Agent File Storage Directory** | `./agentfiles/IT24102445/` |
| **Max File Upload Limit** | 10 MB (`10,485,760 bytes`) |

---

## 🏛️ System Architecture Overview

RemoteOps is a client-server remote management and telemetry framework implemented in C using standard POSIX networking and threading APIs.

```
+-----------------------------------------------------------------------------+
|                               CONTROLLER (Client)                          |
|  - Auto-Authentication (AUTH OPS-2445)                                      |
|  - Interactive Command Line REPL                                            |
|  - Background UDP Listener Thread (Port: User Selected, e.g. 8888)          |
+-----------------------------------------------------------------------------+
               |                                            ^
               | TCP Control & Data Stream (Port 9410)     | Periodic UDP Stats
               v                                            | (SYSINFO) Datagrams
+-----------------------------------------------------------------------------+
|                                AGENT (Server)                               |
|  - Multi-threaded TCP Server (pthread per client)                           |
|  - Authentication & Command Processor                                      |
|  - Whitelisted Remote Command Execution Engine (`popen`)                    |
|  - Chunked Binary File Transfer Engine (`PUT` / `GET`)                      |
|  - Background UDP Telemetry Thread (`MONITOR START / STOP`)                 |
|  - Mutex-Synchronized Audit Logger (`remoteops_IT24102445.log`)             |
|  - Dedicated Storage Sandbox (`./agentfiles/IT24102445/`)                   |
+-----------------------------------------------------------------------------+
```

### Core Architectural Features:
1. **Multi-threaded Concurrency Model**:
   - The **Agent** dynamically allocates worker threads (`pthread_create`) for incoming TCP connections, allowing concurrent client handling without blocking the main socket listener.
   - Detached threads (`pthread_detach`) ensure automatic resource reclamation upon client disconnection.
2. **Dual-Protocol Operations**:
   - **TCP (Port 9410)**: Guarantees reliable transmission for control commands, remote process execution, and binary file transfers (`PUT`/`GET`).
   - **UDP**: Delivers lightweight, periodic telemetry broadcasts (CPU load, memory metrics, uptime) without socket blocking or overhead.
3. **Thread-Safe Audit Logging**:
   - All server events (startup, client connections, authentication attempts, file transfers with measured throughput, and disconnections) are logged with ISO/standard timestamps using a mutex-protected log handler (`pthread_mutex_lock(&log_mutex)`).
4. **Bandwidth & Throughput Telemetry**:
   - File uploads and downloads are measured using high-resolution monotonic clocks (`clock_gettime(CLOCK_MONOTONIC)`), providing real-time data transfer rate metrics in both standard output and audit logs.

---

## 📜 Communication Protocol Specification

All TCP communication adheres to ASCII line-delimited request-response messaging terminated with `\n`. Every agent response is appended with the student's unique session identifier (`SID:5442`).

### Command Reference

| Command | Arguments | Description | Example Server Response |
| :--- | :--- | :--- | :--- |
| `AUTH` | `<token>` | Authenticates client session. Must be the first command. | `OK AUTHENTICATED SID:5442` |
| `SYSINFO` | _None_ | Retrieves agent system metrics (load, memory, uptime). | `OK SYSINFO 0.15 512MB 3600 SID:5442` |
| `LISTPROC` | _None_ | Lists critical running processes. | `OK PROCS 1:init,102:systemd,445:agent SID:5442` |
| `EXEC` | `<CMD>` | Executes a whitelisted system diagnostic command. | `OK EXEC_RESULT <output> SID:5442` |
| `PUT` | `<file> <size>` | Uploads a binary file from Controller to Agent storage. | `OK READY_TO_RECEIVE SID:5442` → `OK FILE_RECEIVED <file> SID:5442` |
| `GET` | `<file>` | Downloads a binary file from Agent storage to Controller. | `OK FILE_SEND <file> <size> SID:5442` |
| `MONITOR START`| `<udp_port>` | Spawns background thread streaming telemetry via UDP every 2s. | `OK MONITOR_STARTED SID:5442` |
| `MONITOR STOP` | _None_ | Stops active UDP telemetry broadcast stream. | `OK MONITOR_STOPPED SID:5442` |
| `QUIT` | _None_ | Gracefully closes the session and disconnects. | `OK BYE SID:5442` |

### Whitelisted `EXEC` Commands
For host security, arbitrary shell execution is blocked. Only the following commands are permitted:
* `DATE` – System date and time (`date`)
* `UPTIME` – Host uptime statistics (`uptime`)
* `DISKFREE` – Disk usage summary (`df -h`)
* `HOSTNAME` – Machine hostname (`hostname`)
* `WHOAMI` – Current execution user (`whoami`)

### Error Codes Reference

| Error Code | Identifier | Description |
| :--- | :--- | :--- |
| `ERR 000` | `USAGE_*` / `UNKNOWN_COMMAND` | Malformed syntax or unrecognized command. |
| `ERR 001` | `AUTH_FAILED` | Invalid or missing authentication token. |
| `ERR 002` | `COMMAND_NOT_ALLOWED` | Command specified in `EXEC` is not in the whitelist. |
| `ERR 003` | `EXEC_FAILED` | Internal error executing the system command via pipe. |
| `ERR 004` | `FILE_TOO_LARGE` | Uploaded file exceeds the 10 MB maximum limit. |
| `ERR 005` | `FILE_NOT_FOUND` | Requested file for download does not exist in storage. |
| `ERR 006` | `CANNOT_WRITE_FILE` | Server failed to create or write destination file. |

---

## 📁 Repository Structure

```
IE3090_IT24102445/
├── Makefile_445                  # Build automation script
├── README.md                     # Comprehensive technical documentation
├── agent_445.c                   # Multi-threaded Agent daemon source code
├── controller_445.c              # Interactive Controller client source code
├── remoteops_IT24102445.log      # Agent execution & audit log file
├── test.txt                      # Sample test payload for file transfer verification
└── agentfiles/
    └── IT24102445/               # Sandboxed Agent file storage directory
```

---

## 🛠️ Build & Compilation

### Prerequisites
* **Compiler**: `gcc` (GNU Compiler Collection) with C99 support
* **Libraries**: POSIX Threads (`-pthread`), standard socket libraries
* **Platform**: Linux, macOS, or Windows via WSL / MinGW

### Building with Makefile
Compile both the `agent_445` and `controller_445` binaries using the designated makefile:

```bash
make -f Makefile_445
```

### Cleaning Build Artifacts
```bash
make -f Makefile_445 clean
```

---

## 🚀 Execution & Usage Guide

### 1. Launching the Agent Daemon
Start the agent in a terminal:
```bash
./agent_445
```
*Output:*
```text
[Agent] Server started on port 9410...
```

### 2. Launching the Controller Client
In a separate terminal or client machine, connect to the agent (defaults to `127.0.0.1`):
```bash
# Connect to local agent
./controller_445

# Or specify a remote agent IP
./controller_445 192.168.1.50
```

### 3. Example Interactive Session

Upon launch, the controller automatically authenticates with `AUTH OPS-2445` and enters the interactive prompt:

```text
[+] Auto-Authentication Successful: OK AUTHENTICATED SID:5442

=== Controller Active (Auto-Authenticated) ===
Type commands to send (e.g. SYSINFO, LISTPROC, PUT <file>, GET <file>, MONITOR START 8888, QUIT):

controller> SYSINFO
Agent Response: OK SYSINFO 0.15 512MB 3600 SID:5442

controller> LISTPROC
Agent Response: OK PROCS 1:init,102:systemd,445:agent SID:5442

controller> EXEC DATE
Agent Response: OK EXEC_RESULT Wed Oct  7 12:15:00 2026 SID:5442

controller> EXEC DISKFREE
Agent Response: OK EXEC_RESULT /dev/sda1 50G 22G 28G 44% / SID:5442

controller> PUT test.txt
Agent Response: OK READY_TO_RECEIVE SID:5442
Agent Response: OK FILE_RECEIVED test.txt SID:5442

controller> GET test.txt
Agent Response: OK FILE_SEND test.txt 24 SID:5442
[+] Download completed: Saved as downloaded_test.txt (24 bytes)

controller> MONITOR START 8888
Agent Response: OK MONITOR_STARTED SID:5442
[UDP Listener] Started on port 8888
[UDP STATS] SYSINFO 0.15 512MB 3600 SID:5442

controller> MONITOR STOP
Agent Response: OK MONITOR_STOPPED SID:5442

controller> QUIT
Connection closed.
```

---

## 📊 Audit Logging Sample

The agent writes detailed, timestamped records into `remoteops_IT24102445.log`:

```log
[Wed Oct  7 12:14:00 2026] Agent service started.
[Wed Oct  7 12:14:05 2026] New TCP connection from 127.0.0.1
[Wed Oct  7 12:14:05 2026] Received command from 127.0.0.1: AUTH OPS-2445
[Wed Oct  7 12:14:05 2026] Authentication successful
[Wed Oct  7 12:14:10 2026] Received command from 127.0.0.1: SYSINFO
[Wed Oct  7 12:14:20 2026] Received command from 127.0.0.1: PUT test.txt 24
[Wed Oct  7 12:14:20 2026] File uploaded: test.txt (Throughput: 124.50 KB/s)
[Wed Oct  7 12:14:30 2026] Received command from 127.0.0.1: GET test.txt
[Wed Oct  7 12:14:30 2026] File downloaded: test.txt (Throughput: 135.20 KB/s)
[Wed Oct  7 12:14:45 2026] Received command from 127.0.0.1: QUIT
[Wed Oct  7 12:14:45 2026] Client disconnected from 127.0.0.1
```

---

## 🛡️ Security & Robustness Features
* **Sandboxed Storage**: All file upload and download operations are confined to `./agentfiles/IT24102445/` to prevent directory traversal vulnerabilities.
* **Payload Size Limits**: Strict 10MB ceiling prevents buffer exhaustion attacks during uploads.
* **Strict Whitelisting**: Remote execution (`EXEC`) rejects unapproved shell commands to safeguard the host system.
* **Thread Safety**: POSIX mutex locks prevent concurrent file write corruptions across parallel client connections in logging routines.
