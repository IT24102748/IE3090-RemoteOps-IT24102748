#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#define PORT 9410
#define BACKLOG 10

#define AUTH_TOKEN "OPS-2748"
#define SESSION_ID "8472"

#define BUFFER_SIZE 8192
#define FILE_BUFFER_SIZE 4096

#define STORAGE_DIR "./agentfiles/IT24102748"

/* ---------------------------------------------------------
   Send all bytes
   --------------------------------------------------------- */
int send_all(int fd, const void *buffer, size_t length)
{
    size_t total = 0;
    const char *data = (const char *)buffer;

    while (total < length)
    {
        ssize_t sent = send(fd, data + total, length - total, 0);

        if (sent <= 0)
        {
            return -1;
        }

        total += sent;
    }

    return 0;
}

/* ---------------------------------------------------------
   Receive exactly the requested number of bytes
   --------------------------------------------------------- */
int recv_all(int fd, void *buffer, size_t length)
{
    size_t total = 0;
    char *data = (char *)buffer;

    while (total < length)
    {
        ssize_t received = recv(fd, data + total, length - total, 0);

        if (received <= 0)
        {
            return -1;
        }

        total += received;
    }

    return 0;
}

/* ---------------------------------------------------------
   Receive one line, ending at '\n'
   This reads ONLY up to the newline so that PUT file bytes
   remain available for the following recv_all().
   --------------------------------------------------------- */
int recv_line(int fd, char *buffer, size_t size)
{
    size_t index = 0;

    while (index < size - 1)
    {
        char c;

        ssize_t received = recv(fd, &c, 1, 0);

        if (received <= 0)
        {
            return -1;
        }

        if (c == '\n')
        {
            break;
        }

        if (c != '\r')
        {
            buffer[index++] = c;
        }
    }

    buffer[index] = '\0';

    return 0;
}

/* ---------------------------------------------------------
   Send protocol response
   --------------------------------------------------------- */
void send_response(int client_fd, const char *response)
{
    send_all(client_fd, response, strlen(response));
}

/* ---------------------------------------------------------
   SYSINFO
   --------------------------------------------------------- */
double get_cpu_load()
{
    FILE *file = fopen("/proc/loadavg", "r");

    if (file == NULL)
    {
        return 0.0;
    }

    double load = 0.0;

    fscanf(file, "%lf", &load);

    fclose(file);

    return load;
}

long get_memory_used_mb()
{
    FILE *file = fopen("/proc/meminfo", "r");

    if (file == NULL)
    {
        return 0;
    }

    long mem_total = 0;
    long mem_available = 0;

    char line[256];

    while (fgets(line, sizeof(line), file))
    {
        if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1)
        {
            continue;
        }

        if (sscanf(line, "MemAvailable: %ld kB", &mem_available) == 1)
        {
            continue;
        }
    }

    fclose(file);

    return (mem_total - mem_available) / 1024;
}

long get_uptime_seconds()
{
    FILE *file = fopen("/proc/uptime", "r");

    if (file == NULL)
    {
        return 0;
    }

    double uptime = 0.0;

    fscanf(file, "%lf", &uptime);

    fclose(file);

    return (long)uptime;
}

void handle_sysinfo(int client_fd)
{
    double cpu = get_cpu_load();
    long memory = get_memory_used_mb();
    long uptime = get_uptime_seconds();

    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK SYSINFO %.2f %ld %ld SID:%s\n",
        cpu,
        memory,
        uptime,
        SESSION_ID
    );

    send_response(client_fd, response);
}

/* ---------------------------------------------------------
   LISTPROC
   --------------------------------------------------------- */
void handle_listproc(int client_fd)
{
    FILE *processes = popen("ps -eo pid,comm --no-headers", "r");

    if (processes == NULL)
    {
        send_response(
            client_fd,
            "ERR 003 PROCESS_LIST_FAILED SID:" SESSION_ID "\n"
        );

        return;
    }

    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK PROCS "
    );

    size_t used = strlen(response);

    char line[256];

    while (fgets(line, sizeof(line), processes) != NULL)
    {
        int pid;
        char command[128];

        if (sscanf(line, "%d %127s", &pid, command) == 2)
        {
            int written = snprintf(
                response + used,
                sizeof(response) - used,
                "[%d %s] ",
                pid,
                command
            );

            if (written < 0 ||
                (size_t)written >= sizeof(response) - used)
            {
                break;
            }

            used += written;
        }
    }

    pclose(processes);

    snprintf(
        response + used,
        sizeof(response) - used,
        "SID:%s\n",
        SESSION_ID
    );

    send_response(client_fd, response);
}

/* ---------------------------------------------------------
   PART K - EXEC whitelist
   --------------------------------------------------------- */
void handle_exec(int client_fd, const char *command)
{
    const char *system_command = NULL;

    /*
       IMPORTANT:
       Only these five commands are allowed.
       User input is NOT directly passed to the shell.
    */

    if (strcmp(command, "EXEC DATE") == 0)
    {
        system_command = "date";
    }
    else if (strcmp(command, "EXEC UPTIME") == 0)
    {
        system_command = "uptime";
    }
    else if (strcmp(command, "EXEC DISKFREE") == 0)
    {
        system_command = "df -h .";
    }
    else if (strcmp(command, "EXEC HOSTNAME") == 0)
    {
        system_command = "hostname";
    }
    else if (strcmp(command, "EXEC WHOAMI") == 0)
    {
        system_command = "whoami";
    }
    else
    {
        send_response(
            client_fd,
            "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID "\n"
        );

        return;
    }

    FILE *process = popen(system_command, "r");

    if (process == NULL)
    {
        send_response(
            client_fd,
            "ERR 003 EXEC_FAILED SID:" SESSION_ID "\n"
        );

        return;
    }

    char output[BUFFER_SIZE];
    char line[512];

    output[0] = '\0';

    while (fgets(line, sizeof(line), process) != NULL)
    {
        size_t current_length = strlen(output);
        size_t remaining = sizeof(output) - current_length - 1;

        if (remaining == 0)
        {
            break;
        }

        strncat(output, line, remaining);
    }

    pclose(process);

    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK EXEC_RESULT %sSID:%s\n",
        output,
        SESSION_ID
    );

    send_response(client_fd, response);
}

/* ---------------------------------------------------------
   Check filename for safe upload
   --------------------------------------------------------- */
int valid_filename(const char *filename)
{
    if (filename == NULL || filename[0] == '\0')
    {
        return 0;
    }

    /*
       Do not allow directory traversal or path separators.
       Only a simple filename is accepted.
    */

    if (strstr(filename, "..") != NULL)
    {
        return 0;
    }

    if (strchr(filename, '/') != NULL)
    {
        return 0;
    }

    if (strchr(filename, '\\') != NULL)
    {
        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------
   PART L - PUT file upload
   --------------------------------------------------------- */
void handle_put(int client_fd, const char *command)
{
    char filename[256];
    unsigned long long filesize;

    /*
       Expected:
       PUT test.txt 29
    */

    int parsed = sscanf(
        command,
        "PUT %255s %llu",
        filename,
        &filesize
    );

    if (parsed != 2)
    {
        send_response(
            client_fd,
            "ERR 004 INVALID_PUT SID:" SESSION_ID "\n"
        );

        return;
    }

    if (!valid_filename(filename))
    {
        send_response(
            client_fd,
            "ERR 004 INVALID_FILENAME SID:" SESSION_ID "\n"
        );

        return;
    }

    /*
       Prevent unreasonable file sizes from overflowing
       the local system.
    */

    if (filesize > 1024ULL * 1024ULL * 100ULL)
    {
        send_response(
            client_fd,
            "ERR 004 FILE_TOO_LARGE SID:" SESSION_ID "\n"
        );

        return;
    }

    /*
       Create personalized storage directory.
    */
    if (mkdir("./agentfiles", 0755) == -1 && errno != EEXIST)
    {
        send_response(
            client_fd,
            "ERR 005 STORAGE_ERROR SID:" SESSION_ID "\n"
        );

        return;
    }

    if (mkdir(STORAGE_DIR, 0755) == -1 && errno != EEXIST)
    {
        send_response(
            client_fd,
            "ERR 005 STORAGE_ERROR SID:" SESSION_ID "\n"
        );

        return;
    }

    char filepath[512];

    snprintf(
        filepath,
        sizeof(filepath),
        "%s/%s",
        STORAGE_DIR,
        filename
    );

    FILE *file = fopen(filepath, "wb");

    if (file == NULL)
    {
        send_response(
            client_fd,
            "ERR 005 FILE_OPEN_FAILED SID:" SESSION_ID "\n"
        );

        return;
    }

    char buffer[FILE_BUFFER_SIZE];

    unsigned long long remaining = filesize;

    while (remaining > 0)
    {
        size_t chunk;

        if (remaining > FILE_BUFFER_SIZE)
        {
            chunk = FILE_BUFFER_SIZE;
        }
        else
        {
            chunk = (size_t)remaining;
        }

        ssize_t received = recv(
            client_fd,
            buffer,
            chunk,
            0
        );

        if (received <= 0)
        {
            fclose(file);

            remove(filepath);

            return;
        }

        size_t written = fwrite(
            buffer,
            1,
            (size_t)received,
            file
        );

        if (written != (size_t)received)
        {
            fclose(file);

            remove(filepath);

            send_response(
                client_fd,
                "ERR 005 FILE_WRITE_FAILED SID:" SESSION_ID "\n"
            );

            return;
        }

        remaining -= (unsigned long long)received;
    }

    fclose(file);

    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK FILE_RECEIVED %s SID:%s\n",
        filename,
        SESSION_ID
    );

    send_response(client_fd, response);
}

/* ---------------------------------------------------------
   Handle each Controller connection
   --------------------------------------------------------- */
void *handle_controller(void *arg)
{
    int client_fd = *(int *)arg;

    free(arg);

    int authenticated = 0;

    char command[BUFFER_SIZE];

    while (1)
    {
        int result = recv_line(
            client_fd,
            command,
            sizeof(command)
        );

        if (result < 0)
        {
            break;
        }

        if (strlen(command) == 0)
        {
            continue;
        }

        printf(
            "Received command: %s\n",
            command
        );

        /* ---------------------------------------------
           AUTH
           --------------------------------------------- */

        if (strncmp(command, "AUTH ", 5) == 0)
        {
            const char *token = command + 5;

            if (strcmp(token, AUTH_TOKEN) == 0)
            {
                authenticated = 1;

                send_response(
                    client_fd,
                    "OK AUTHENTICATED SID:" SESSION_ID "\n"
                );

                printf(
                    "Controller authenticated.\n"
                );
            }
            else
            {
                send_response(
                    client_fd,
                    "ERR 001 AUTH_FAILED SID:" SESSION_ID "\n"
                );

                printf(
                    "Authentication failed.\n"
                );
            }

            continue;
        }

        /* ---------------------------------------------
           All other commands require authentication
           --------------------------------------------- */

        if (!authenticated)
        {
            send_response(
                client_fd,
                "ERR 001 AUTH_FAILED SID:" SESSION_ID "\n"
            );

            continue;
        }

        /* ---------------------------------------------
           EXIT
           --------------------------------------------- */

        if (strcmp(command, "EXIT") == 0)
        {
            break;
        }

        /* ---------------------------------------------
           SYSINFO
           --------------------------------------------- */

        if (strcmp(command, "SYSINFO") == 0)
        {
            handle_sysinfo(client_fd);
        }

        /* ---------------------------------------------
           LISTPROC
           --------------------------------------------- */

        else if (strcmp(command, "LISTPROC") == 0)
        {
            handle_listproc(client_fd);
        }

        /* ---------------------------------------------
           PART K - EXEC
           --------------------------------------------- */

        else if (strncmp(command, "EXEC ", 5) == 0)
        {
            handle_exec(
                client_fd,
                command
            );
        }

        /* ---------------------------------------------
           PART L - PUT
           --------------------------------------------- */

        else if (strncmp(command, "PUT ", 4) == 0)
        {
            handle_put(
                client_fd,
                command
            );
        }

        /* ---------------------------------------------
           Unknown command
           --------------------------------------------- */

        else
        {
            send_response(
                client_fd,
                "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID "\n"
            );
        }
    }

    close(client_fd);

    printf(
        "Controller disconnected.\n"
    );

    return NULL;
}

/* ---------------------------------------------------------
   MAIN - Agent TCP server
   --------------------------------------------------------- */
int main()
{
    int server_fd;

    struct sockaddr_in server_address;

    printf("Agent starting...\n");

    /* Create socket */
    server_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    /* Allow reuse of port */
    int option = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &option,
            sizeof(option)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    memset(
        &server_address,
        0,
        sizeof(server_address)
    );

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    /* Bind */
    if (bind(
            server_fd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    /* Listen */
    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Port: %d\n", PORT);
    printf("Listening...\n");

    /* Accept Controllers */
    while (1)
    {
        struct sockaddr_in client_address;

        socklen_t client_length =
            sizeof(client_address);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_address,
            &client_length
        );

        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        printf("Controller connected.\n");

        int *client_socket =
            malloc(sizeof(int));

        if (client_socket == NULL)
        {
            perror("malloc");
            close(client_fd);
            continue;
        }

        *client_socket = client_fd;

        pthread_t thread_id;

        if (pthread_create(
                &thread_id,
                NULL,
                handle_controller,
                client_socket) != 0)
        {
            perror("pthread_create");

            free(client_socket);
            close(client_fd);

            continue;
        }

        pthread_detach(thread_id);
    }

    close(server_fd);

    return 0;
}
