#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410
#define AUTH_TOKEN "OPS-2748"
#define BUFFER_SIZE 8192
#define FILE_BUFFER_SIZE 4096
#define UDP_BUFFER_SIZE 2048

typedef struct
{
    int running;
    int udp_socket;
    unsigned short udp_port;
    pthread_t thread;
} MonitorState;

MonitorState monitor = {0};

/* ============================================================
   SEND ALL
   ============================================================ */

int send_all(int socket_fd, const void *buffer, size_t length)
{
    size_t total_sent = 0;
    const char *data = (const char *)buffer;

    while (total_sent < length)
    {
        ssize_t sent = send(
            socket_fd,
            data + total_sent,
            length - total_sent,
            0
        );

        if (sent <= 0)
        {
            return -1;
        }

        total_sent += sent;
    }

    return 0;
}

/* ============================================================
   RECEIVE ALL
   ============================================================ */

int recv_all(int socket_fd, void *buffer, size_t length)
{
    size_t total_received = 0;
    char *data = (char *)buffer;

    while (total_received < length)
    {
        ssize_t received = recv(
            socket_fd,
            data + total_received,
            length - total_received,
            0
        );

        if (received <= 0)
        {
            return -1;
        }

        total_received += received;
    }

    return 0;
}

/* ============================================================
   RECEIVE ONE LINE
   ============================================================ */

int recv_line(int socket_fd, char *buffer, size_t buffer_size)
{
    size_t index = 0;

    while (index < buffer_size - 1)
    {
        char character;

        ssize_t received = recv(
            socket_fd,
            &character,
            1,
            0
        );

        if (received <= 0)
        {
            return -1;
        }

        if (character == '\n')
        {
            break;
        }

        if (character != '\r')
        {
            buffer[index++] = character;
        }
    }

    buffer[index] = '\0';

    return 0;
}

/* ============================================================
   REMOVE NEWLINE
   ============================================================ */

void remove_newline(char *text)
{
    if (text == NULL)
    {
        return;
    }

    text[strcspn(text, "\r\n")] = '\0';
}

/* ============================================================
   RECEIVE NORMAL RESPONSE
   ============================================================ */

int receive_response(int socket_fd)
{
    char response[BUFFER_SIZE];

    if (recv_line(socket_fd, response, sizeof(response)) < 0)
    {
        printf("Connection closed or response receive failed.\n");
        return -1;
    }

    printf("Agent response:\n%s\n", response);

    return 0;
}

/* ============================================================
   SEND NORMAL COMMAND
   ============================================================ */

int send_command(int socket_fd, const char *command)
{
    char message[BUFFER_SIZE];

    snprintf(
        message,
        sizeof(message),
        "%s\n",
        command
    );

    printf("Sending: %s\n", command);

    if (send_all(
            socket_fd,
            message,
            strlen(message)) < 0)
    {
        perror("send");
        return -1;
    }

    return receive_response(socket_fd);
}

/* ============================================================
   PUT FILE
   ============================================================ */

int upload_file(int socket_fd, const char *filename)
{
    FILE *file;
    unsigned long long file_size;
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char buffer[FILE_BUFFER_SIZE];

    file = fopen(filename, "rb");

    if (file == NULL)
    {
        perror("Unable to open file");
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        perror("fseek");
        fclose(file);
        return -1;
    }

    long size = ftell(file);

    if (size < 0)
    {
        perror("ftell");
        fclose(file);
        return -1;
    }

    file_size = (unsigned long long)size;

    rewind(file);

    printf("Uploading file: %s\n", filename);
    printf("File size: %llu bytes\n", file_size);

    snprintf(
        command,
        sizeof(command),
        "PUT %s %llu\n",
        filename,
        file_size
    );

    printf("Sending: PUT %s %llu\n", filename, file_size);

    if (send_all(
            socket_fd,
            command,
            strlen(command)) < 0)
    {
        perror("send");
        fclose(file);
        return -1;
    }

    unsigned long long total_sent = 0;

    while (total_sent < file_size)
    {
        size_t bytes_to_read = FILE_BUFFER_SIZE;

        if (file_size - total_sent < bytes_to_read)
        {
            bytes_to_read =
                (size_t)(file_size - total_sent);
        }

        size_t bytes_read =
            fread(buffer, 1, bytes_to_read, file);

        if (bytes_read == 0)
        {
            if (ferror(file))
            {
                perror("fread");
            }

            fclose(file);
            return -1;
        }

        if (send_all(
                socket_fd,
                buffer,
                bytes_read) < 0)
        {
            perror("send file data");
            fclose(file);
            return -1;
        }

        total_sent += bytes_read;
    }

    fclose(file);

    printf("File bytes sent successfully.\n");

    if (recv_line(
            socket_fd,
            response,
            sizeof(response)) < 0)
    {
        printf("Failed to receive PUT response.\n");
        return -1;
    }

    printf("Agent response:\n%s\n", response);

    return 0;
}

/* ============================================================
   GET / DOWNLOAD FILE
   ============================================================ */

int download_file(int socket_fd, const char *filename)
{
    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    printf("Sending: GET %s\n", filename);

    snprintf(
        command,
        sizeof(command),
        "GET %s\n",
        filename
    );

    if (send_all(
            socket_fd,
            command,
            strlen(command)) < 0)
    {
        perror("send");
        return -1;
    }

    /*
     * IMPORTANT:
     *
     * The Agent first sends one text header:
     *
     * OK FILE_SEND filename filesize SID:8472
     *
     * Then it sends the raw file bytes.
     *
     * We must read the header separately before
     * receiving the file.
     */

    if (recv_line(
            socket_fd,
            response,
            sizeof(response)) < 0)
    {
        printf("Failed to receive GET response.\n");
        return -1;
    }

    printf("Agent response:\n%s\n", response);

    /*
     * Check whether the Agent returned an error.
     */

    if (strncmp(response, "ERR ", 4) == 0)
    {
        return -1;
    }

    /*
     * Parse:
     *
     * OK FILE_SEND test.txt 29 SID:8472
     */

    char received_filename[256];
    char session_id[64];
    unsigned long long file_size;

    int parsed = sscanf(
        response,
        "OK FILE_SEND %255s %llu SID:%63s",
        received_filename,
        &file_size,
        session_id
    );

    if (parsed != 3)
    {
        printf("Invalid GET response format.\n");
        return -1;
    }

    /*
     * Create local output filename:
     *
     * downloaded_test.txt
     */

    char output_filename[512];

    snprintf(
        output_filename,
        sizeof(output_filename),
        "downloaded_%s",
        received_filename
    );

    FILE *file = fopen(output_filename, "wb");

    if (file == NULL)
    {
        perror("Unable to create downloaded file");
        return -1;
    }

    /*
     * Receive exactly file_size bytes.
     */

    char buffer[FILE_BUFFER_SIZE];

    unsigned long long total_received = 0;

    while (total_received < file_size)
    {
        size_t bytes_to_receive = FILE_BUFFER_SIZE;

        if (file_size - total_received < bytes_to_receive)
        {
            bytes_to_receive =
                (size_t)(file_size - total_received);
        }

        ssize_t received = recv(
            socket_fd,
            buffer,
            bytes_to_receive,
            0
        );

        if (received <= 0)
        {
            printf("Connection closed while downloading file.\n");
            fclose(file);
            return -1;
        }

        size_t written = fwrite(
            buffer,
            1,
            received,
            file
        );

        if (written != (size_t)received)
        {
            perror("File write error");
            fclose(file);
            return -1;
        }

        total_received += received;
    }

    fclose(file);

    printf("File downloaded successfully.\n");
    printf("Saved as: %s\n", output_filename);
    printf("Bytes received: %llu\n", total_received);

    return 0;
}

/* ============================================================
   UDP MONITOR THREAD
   ============================================================ */

void *monitor_thread_function(void *arg)
{
    (void)arg;

    struct sockaddr_in agent_address;

    memset(
        &agent_address,
        0,
        sizeof(agent_address)
    );

    agent_address.sin_family = AF_INET;

    /*
     * The UDP monitor receives packets from the Agent.
     * The Agent sends them to the Controller's UDP port.
     */

    printf("UDP monitor listening on port %u...\n",
           monitor.udp_port);

    while (monitor.running)
    {
        fd_set readfds;

        FD_ZERO(&readfds);
        FD_SET(monitor.udp_socket, &readfds);

        struct timeval timeout;

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int result = select(
            monitor.udp_socket + 1,
            &readfds,
            NULL,
            NULL,
            &timeout
        );

        if (!monitor.running)
        {
            break;
        }

        if (result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("select");
            break;
        }

        if (result == 0)
        {
            continue;
        }

        if (FD_ISSET(
                monitor.udp_socket,
                &readfds))
        {
            char buffer[UDP_BUFFER_SIZE];

            socklen_t address_length =
                sizeof(agent_address);

            ssize_t received = recvfrom(
                monitor.udp_socket,
                buffer,
                sizeof(buffer) - 1,
                0,
                (struct sockaddr *)&agent_address,
                &address_length
            );

            if (received > 0)
            {
                buffer[received] = '\0';

                printf("\n[UDP MONITOR] %s\n",
                       buffer);

                printf("Enter command: ");
                fflush(stdout);
            }
        }
    }

    return NULL;
}

/* ============================================================
   START UDP MONITOR
   ============================================================ */

int start_monitor(unsigned short port)
{
    if (monitor.running)
    {
        printf("Monitor is already running.\n");
        return -1;
    }

    monitor.udp_socket = socket(
        AF_INET,
        SOCK_DGRAM,
        0
    );

    if (monitor.udp_socket < 0)
    {
        perror("UDP socket");
        return -1;
    }

    int reuse = 1;

    setsockopt(
        monitor.udp_socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse,
        sizeof(reuse)
    );

    struct sockaddr_in address;

    memset(
        &address,
        0,
        sizeof(address)
    );

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(
            monitor.udp_socket,
            (struct sockaddr *)&address,
            sizeof(address)) < 0)
    {
        perror("UDP bind");
        close(monitor.udp_socket);
        monitor.udp_socket = -1;
        return -1;
    }

    monitor.udp_port = port;
    monitor.running = 1;

    if (pthread_create(
            &monitor.thread,
            NULL,
            monitor_thread_function,
            NULL) != 0)
    {
        perror("pthread_create");
        monitor.running = 0;
        close(monitor.udp_socket);
        monitor.udp_socket = -1;
        return -1;
    }

    return 0;
}

/* ============================================================
   STOP UDP MONITOR
   ============================================================ */

void stop_monitor(void)
{
    if (!monitor.running)
    {
        return;
    }

    monitor.running = 0;

    /*
     * Wait for the monitor thread to finish.
     */

    pthread_join(
        monitor.thread,
        NULL
    );

    close(monitor.udp_socket);

    monitor.udp_socket = -1;

    printf("UDP monitor stopped.\n");
}

/* ============================================================
   MAIN
   ============================================================ */

int main(void)
{
    int socket_fd;

    struct sockaddr_in server_address;

    printf("Controller starting...\n");

    /*
     * Create TCP socket.
     */

    socket_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (socket_fd < 0)
    {
        perror("socket");
        return 1;
    }

    /*
     * Configure Agent address.
     */

    memset(
        &server_address,
        0,
        sizeof(server_address)
    );

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);

    if (inet_pton(
            AF_INET,
            SERVER_IP,
            &server_address.sin_addr) <= 0)
    {
        printf("Invalid Agent IP address.\n");
        close(socket_fd);
        return 1;
    }

    printf(
        "Connecting to %s:%d...\n",
        SERVER_IP,
        PORT
    );

    /*
     * Connect to Agent.
     */

    if (connect(
            socket_fd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)) < 0)
    {
        perror("connect");
        close(socket_fd);
        return 1;
    }

    printf("Connected to Agent successfully.\n");

    /*
     * Authenticate automatically.
     */

    char auth_command[BUFFER_SIZE];

    snprintf(
        auth_command,
        sizeof(auth_command),
        "AUTH %s\n",
        AUTH_TOKEN
    );

    printf("Sending: AUTH %s\n", AUTH_TOKEN);

    if (send_all(
            socket_fd,
            auth_command,
            strlen(auth_command)) < 0)
    {
        perror("send");
        close(socket_fd);
        return 1;
    }

    if (receive_response(socket_fd) < 0)
    {
        close(socket_fd);
        return 1;
    }

    /*
     * Main command loop.
     */

    char command[BUFFER_SIZE];

    while (1)
    {
        printf("\nEnter command: ");
        fflush(stdout);

        if (fgets(
                command,
                sizeof(command),
                stdin) == NULL)
        {
            break;
        }

        remove_newline(command);

        if (strlen(command) == 0)
        {
            continue;
        }

        /*
         * EXIT
         */

        if (strcmp(command, "QUIT") == 0)
	{
    		printf("Sending: QUIT\n");

    		if (send_all(
        	    socket_fd,
        	    "QUIT\n",
        	    strlen("QUIT\n")) < 0)
    		{
        		perror("send");
    		}
        	else
        	{
        		char response[BUFFER_SIZE];

        		if (recv_line(
                		socket_fd,
                		response,
                		sizeof(response)) == 0)
        		{
        		    printf("Agent response:\n%s\n", response);
        		}
        	}

    		/*
    		 * Stop local UDP monitoring if active.
    		 */

   		 if (monitor.running)
    		 {
        		stop_monitor();
     		 }

    		 close(socket_fd);

         	printf("Connection closed.\n");

         	break;
	}

        /*
         * PUT
         */

        if (strncmp(command, "PUT ", 4) == 0)
        {
            char filename[256];

            if (sscanf(
                    command,
                    "PUT %255s",
                    filename) != 1)
            {
                printf("Invalid PUT command.\n");
                continue;
            }

            upload_file(
                socket_fd,
                filename
            );

            continue;
        }

        /*
         * GET
         */

        if (strncmp(command, "GET ", 4) == 0)
        {
            char filename[256];

            if (sscanf(
                    command,
                    "GET %255s",
                    filename) != 1)
            {
                printf("Invalid GET command.\n");
                continue;
            }

            download_file(
                socket_fd,
                filename
            );

            continue;
        }

        /*
         * MONITOR START
         */

        if (strncmp(
                command,
                "MONITOR START ",
                14) == 0)
        {
            unsigned int port;

            if (sscanf(
                    command + 14,
                    "%u",
                    &port) != 1 ||
                port < 1024 ||
                port > 65535)
            {
                printf("Invalid UDP port.\n");
                continue;
            }

            if (start_monitor(
                    (unsigned short)port) != 0)
            {
                continue;
            }

            /*
             * Tell Agent to start monitoring.
             */

            char monitor_command[BUFFER_SIZE];

            snprintf(
                monitor_command,
                sizeof(monitor_command),
                "MONITOR START %u",
                port
            );

            if (send_command(
                    socket_fd,
                    monitor_command) < 0)
            {
                stop_monitor();
            }

            continue;
        }

        /*
         * MONITOR STOP
         */

        if (strcmp(
                command,
                "MONITOR STOP") == 0)
        {
            send_command(
                socket_fd,
                command
            );

            stop_monitor();

            continue;
        }

        /*
         * Normal commands:
         *
         * SYSINFO
         * LISTPROC
         * EXEC DATE
         * EXEC UPTIME
         * EXEC DISKFREE
         * EXEC HOSTNAME
         * EXEC WHOAMI
         */

        if (send_command(
                socket_fd,
                command) < 0)
        {
            break;
        }
    }

    /*
     * Clean up monitor.
     */

    if (monitor.running)
    {
        stop_monitor();
    }

    close(socket_fd);

    printf("Controller terminated.\n");

    return 0;
}
