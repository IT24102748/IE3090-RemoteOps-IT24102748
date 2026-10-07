# Design Diary — RemoteOps

## Date: 06/10/2026

### Decision
I selected a thread-based concurrency model for the RemoteOps Agent.

### Reason
The Agent needs to support multiple Controller connections. Using a separate thread for each connected Controller allows the Agent to handle clients concurrently without blocking other connections.

### Problem
The first implementation of command receiving assumed that one `recv()` call would always contain one complete command.

### Solution
I changed the command receiving logic to use line-based framing and buffer incoming TCP data until a newline character is received.

### What I learned
TCP is a byte-stream protocol and does not preserve application-level message boundaries. Therefore, the application must implement its own framing mechanism.


## Date: 06/10/2026

### Decision
I implemented authentication using the required authentication token and generated session ID.

### Reason
The RemoteOps protocol requires the Controller to authenticate before using protected Agent operations.

### Problem
Commands should not be accepted before authentication.

### Solution
The Agent checks the authentication command and only allows protected operations after successful authentication. The session ID is included in protocol responses.

### What I learned
Authentication should be performed before allowing access to remote operations, and protocol responses can contain session information to identify the active session.


## Date: 07/10/2026

### Decision
I implemented PUT and GET using explicit file sizes.

### Reason
File transfers contain arbitrary binary data, so the receiver needs to know exactly how many bytes belong to the file.

### Problem
The initial GET implementation returned the file header and raw file data, but the Controller did not correctly separate the header from the file bytes.

### Solution
I updated the Controller to read the `FILE_SEND` header first, extract the file size, receive exactly that number of bytes, and save them as a local downloaded file.

### What I learned
File-transfer protocols need explicit framing and byte counts. Raw file data should not be treated as a normal text response.


## Date: 07/10/2026

### Decision
I added logging and abnormal-disconnection handling.

### Reason
The assignment requires evidence of connections, commands, file transfers and disconnections, and the Agent should remain available if a Controller terminates unexpectedly.

### Problem
A Controller can terminate without sending the normal QUIT command.

### Solution
The Agent detects the closed TCP connection, records the disconnection in the log, cleans up the client connection and continues listening for new Controllers.

### What I learned
Network programs must handle unexpected client termination as well as normal protocol termination.


## Date: 07/10/2026

### Decision
I added a Makefile and project documentation.

### Reason
A Makefile provides a consistent way to compile the Agent and Controller, while the README documents the architecture, protocol commands, build process and testing.

### Problem
Manually compiling multiple C source files can result in inconsistent compiler options or executable names.

### Solution
I created `Makefile_748` using GCC with `-Wall -Wextra -pthread` and documented the build procedure in the README.

### What I learned
Build automation and documentation make a network programming project easier to test, reproduce and maintain.
