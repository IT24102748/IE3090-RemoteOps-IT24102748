# RemoteOps – IE3090 Network Programming

## Student Information

**Registration Number:** IT24102748  
**Student:** Shashitha Randika  
**Module:** IE3090 Network Programming

---

## Project Description

RemoteOps is a client-server remote operations system developed for the IE3090 Network Programming assignment.

The system consists of two main programs:

- `agent_748.c` – TCP server/Agent
- `controller_748.c` – TCP client/Controller

The Controller connects to the Agent using TCP, authenticates using a predefined token, and sends authorized remote operation commands.

The Agent supports multiple simultaneous Controller connections using POSIX threads.

---

## Network Configuration

| Item | Value |
|---|---|
| Protocol | TCP |
| Agent IP | 127.0.0.1 |
| Agent Port | 9410 |
| Session ID | SID:8472 |
| Authentication Token | OPS-2748 |
| UDP Monitoring Port | 9500 |
| Storage Directory | `./agentfiles/IT24102748/` |
| Log File | `remoteops_IT24102748.log` |

---

## Source Files

### Agent

```text
agent_748.c
```

The Agent acts as the TCP server. It accepts Controller connections, authenticates clients, processes commands, handles file transfers, provides monitoring information, and records system activity in the log file.

### Controller

```text
controller_748.c
```

The Controller acts as the TCP client. It connects to the Agent, authenticates, sends commands, uploads and downloads files, receives UDP monitoring information, and handles clean termination.

### Makefile

```text
Makefile_748
```

The Makefile provides an easy way to compile both programs.

---

## Supported Commands

### AUTH

Authenticates the Controller with the Agent.

```text
AUTH OPS-2748
```

Expected response:

```text
OK AUTHENTICATED SID:8472
```

---

### SYSINFO

Requests system information from the Agent.

```text
SYSINFO
```

---

### LISTPROC

Requests a list of running processes.

```text
LISTPROC
```

---

### EXEC

The Agent implements a command whitelist for safe execution.

Supported commands include:

```text
EXEC DATE
EXEC UPTIME
EXEC DISKFREE
EXEC HOSTNAME
EXEC WHOAMI
```

Unauthorized commands are rejected.

Example:

```text
EXEC DATE
```

---

### PUT

Uploads a file from the Controller to the Agent.

Example:

```text
PUT test.txt
```

Uploaded files are stored in:

```text
./agentfiles/IT24102748/
```

The transfer protocol includes the exact file size so that the Agent can receive the correct number of bytes.

---

### GET

Downloads a file from the Agent to the Controller.

Example:

```text
GET test.txt
```

The downloaded file is saved by the Controller using the format:

```text
downloaded_<filename>
```

For example:

```text
downloaded_test.txt
```

---

### MONITOR START

Starts UDP system monitoring.

Example:

```text
MONITOR START 9500
```

The Agent periodically sends system information to the Controller using UDP.

---

### MONITOR STOP

Stops UDP monitoring.

```text
MONITOR STOP
```

---

### QUIT

Terminates the Controller session cleanly.

```text
QUIT
```

Expected response:

```text
OK BYE SID:8472
```

If monitoring is active, it is stopped before the TCP connection is closed.

---

## TCP Framing

The RemoteOps protocol uses newline-delimited commands.

The implementation does not assume that one TCP `recv()` contains one complete command.

The Agent uses `recv_line()` to continue receiving bytes until a newline character is found.

Conceptually:

```text
TCP byte stream
      |
      v
receive bytes
      |
      v
find newline
      |
      v
complete command
      |
      v
process command
```

This allows the system to handle partial TCP receives.

For example, a command such as:

```text
SYSINFO\n
```

could arrive in multiple TCP segments. The Agent continues receiving until the complete line is available.

---

## File Transfer Framing

File transfers use an explicit file size.

For PUT:

```text
PUT filename filesize
```

The Agent reads the file size and receives exactly that number of bytes.

For GET, the Agent first sends a header such as:

```text
OK FILE_SEND test.txt 29 SID:8472
```

followed by exactly 29 file bytes.

This prevents the application from depending on TCP packet boundaries.

---

## TCP Reliability

The implementation uses `send_all()` to handle partial TCP sends.

The system also uses exact byte-count loops for file transfers.

Therefore:

```text
send()
  |
  +-- partial send
  |
  +-- remaining bytes
  |
  +-- send again
  |
  v
complete transmission
```

---

## Authentication

The Agent requires authentication before allowing normal operations.

Authentication token:

```text
OPS-2748
```

Session identifier:

```text
SID:8472
```

Unauthenticated commands are rejected.

---

## Security

The `EXEC` command uses a whitelist rather than allowing arbitrary shell commands.

Allowed commands are:

```text
DATE
UPTIME
DISKFREE
HOSTNAME
WHOAMI
```

Other commands return:

```text
ERR 002 COMMAND_NOT_ALLOWED SID:8472
```

File names are also validated to prevent path traversal such as:

```text
../file
../../file
/etc/passwd
```

Uploaded files are stored in the personalized directory:

```text
./agentfiles/IT24102748/
```

---

## Logging

The Agent records important activities in:

```text
remoteops_IT24102748.log
```

Logged activities include:

- Agent startup
- Controller connections
- Authentication
- Commands received
- File uploads
- File downloads
- UDP monitoring
- QUIT requests
- Controller disconnections
- Abnormal Controller disconnections

Each log entry includes a timestamp.

Example:

```text
[2026-10-07 16:40:01] Controller connected
[2026-10-07 16:40:05] Command received: SYSINFO
[2026-10-07 16:40:20] File upload completed: test.txt (29 bytes)
[2026-10-07 16:40:25] File download completed: test.txt (29 bytes)
[2026-10-07 16:40:30] Controller requested QUIT
[2026-10-07 16:40:30] Controller disconnected
```

---

## Build Instructions

The complete project can be compiled using:

```bash
make -f Makefile_748
```

This generates:

```text
agent_748
controller_748
```

To remove the compiled executables:

```bash
make -f Makefile_748 clean
```

---

## Running the Agent

Start the Agent with:

```bash
./agent_748
```

The Agent listens on:

```text
TCP port 9410
```

---

## Running the Controller

In another terminal:

```bash
./controller_748
```

The Controller connects to:

```text
127.0.0.1:9410
```

---

## Example Session

```text
Controller starting...
Connecting to 127.0.0.1:9410...
Connected to Agent successfully.
Sending: AUTH OPS-2748
Agent response:
OK AUTHENTICATED SID:8472

Enter command: SYSINFO
Sending: SYSINFO
Agent response:
OK SYSINFO ...

Enter command: PUT test.txt
Uploading file: test.txt
File size: 29 bytes

Enter command: GET test.txt
Sending: GET test.txt
Agent response:
OK FILE_SEND test.txt 29 SID:8472
File downloaded successfully.
Saved as: downloaded_test.txt
Bytes received: 29

Enter command: QUIT
Sending: QUIT
Agent response:
OK BYE SID:8472
Connection closed.
```

---

## Abnormal Disconnect Handling

The Agent handles unexpected Controller termination.

If the Controller terminates without sending `QUIT`, the Agent detects the TCP disconnect, records the event in the log, cleans up the Controller connection, and continues listening for new Controller connections.

This allows the Agent to remain operational after an ungraceful client termination.

---

## Project Directory

The main project structure is:

```text
IE3090_RemoteOps/
│
├── agent_748.c
├── controller_748.c
├── Makefile_748
├── README.md
├── design_diary.md
├── prompt_log.md
├── reflection.md
├── remoteops_IT24102748.log
│
├── agentfiles/
│   └── IT24102748/
│       └── uploaded files
│
└── test files
```

---

## Build Command

```bash
make -f Makefile_748
```

## Run Commands

Agent:

```bash
./agent_748
```

Controller:

```bash
./controller_748
```

## Log Command

```bash
tail -n 30 remoteops_IT24102748.log
```

## File Verification

Example:

```bash
cmp test.txt downloaded_test.txt
```

If no output is produced by `cmp`, the files are identical.
