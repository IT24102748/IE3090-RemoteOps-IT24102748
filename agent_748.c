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

#define PORT 9410
#define BACKLOG 10

#define AUTH_TOKEN "OPS-2748"
#define SESSION_ID "8472"

#define BUFFER_SIZE 8192
#define FILE_BUFFER_SIZE 4096

#define STORAGE_DIR "./agentfiles/IT24102748"

#define MONITOR_INTERVAL 2

/* =========================================================
   Function declarations
   ========================================================= */

void *handle_controller(void *arg);

void handle_get(int client_fd, const char *filename);

void handle_put(
    int client_fd,
    const char *filename,
    unsigned long long file_size
);

/* =========================================================
   Send all bytes
   ========================================================= */

int send_all(
    int socket_fd,
    const void *buffer,
    size_t length
)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(
            socket_fd,
            (const char *)buffer + total_sent,
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

/* =========================================================
   Receive all bytes
   ========================================================= */

int recv_all(
    int socket_fd,
    void *buffer,
    size_t length
)
{
    size_t total_received = 0;

    while (total_received < length)
    {
        ssize_t received = recv(
            socket_fd,
            (char *)buffer + total_received,
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

/* =========================================================
   Receive one line
   ========================================================= */

int recv_line(
    int socket_fd,
    char *buffer,
    size_t buffer_size
)
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

    return (int)index;
}

/* =========================================================
   Send response
   ========================================================= */

void send_response(
    int client_fd,
    const char *message
)
{
    send_all(
        client_fd,
        message,
        strlen(message)
    );
}

/* =========================================================
   Validate filename
   Prevent path traversal
   ========================================================= */

int valid_filename(const char *filename)
{
    if (filename == NULL)
    {
        return 0;
    }

    if (strlen(filename) == 0)
    {
        return 0;
    }

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

/* =========================================================
   SYSINFO
   ========================================================= */

void handle_sysinfo(int client_fd)
{
    FILE *file;

    double cpu_load = 0.0;
    long memory_used_mb = 0;
    long uptime_seconds = 0;

    /* CPU load */

    file = fopen("/proc/loadavg", "r");

    if (file != NULL)
    {
        fscanf(file, "%lf", &cpu_load);
        fclose(file);
    }

    /* Memory */

    file = fopen("/proc/meminfo", "r");

    if (file != NULL)
    {
        long mem_total_kb = 0;
        long mem_available_kb = 0;

        char line[256];

        while (fgets(line, sizeof(line), file))
        {
            if (sscanf(
                    line,
                    "MemTotal: %ld kB",
                    &mem_total_kb
                ) == 1)
            {
                continue;
            }

            if (sscanf(
                    line,
                    "MemAvailable: %ld kB",
                    &mem_available_kb
                ) == 1)
            {
                continue;
            }
        }

        fclose(file);

        if (mem_total_kb > 0)
        {
            memory_used_mb =
                (mem_total_kb - mem_available_kb) / 1024;
        }
    }

    /* Uptime */

    file = fopen("/proc/uptime", "r");

    if (file != NULL)
    {
        double uptime;

        if (fscanf(file, "%lf", &uptime) == 1)
        {
            uptime_seconds = (long)uptime;
        }

        fclose(file);
    }

    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK SYSINFO %.2f %ld %ld SID:%s\n",
        cpu_load,
        memory_used_mb,
        uptime_seconds,
        SESSION_ID
    );

    send_response(client_fd, response);
}

/* =========================================================
   LISTPROC
   ========================================================= */

void handle_listproc(int client_fd)
{
    FILE *process_file;

    process_file = popen(
        "ps -eo pid,comm --no-headers",
        "r"
    );

    if (process_file == NULL)
    {
        send_response(
            client_fd,
            "ERR 004 INTERNAL_ERROR SID:" SESSION_ID "\n"
        );

        return;
    }

    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK PROCS "
    );

    char line[256];

    while (fgets(line, sizeof(line), process_file))
    {
        size_t current_length = strlen(response);
        size_t remaining =
            sizeof(response) - current_length - 1;

        if (remaining <= 1)
        {
            break;
        }

        line[strcspn(line, "\r\n")] = '\0';

        strncat(
            response,
            line,
            remaining
        );

        remaining =
            sizeof(response) - strlen(response) - 1;

        if (remaining > 1)
        {
            strncat(
                response,
                ";",
                remaining
            );
        }
    }

    pclose(process_file);

    strncat(
        response,
        "SID:" SESSION_ID "\n",
        sizeof(response) - strlen(response) - 1
    );

    send_response(client_fd, response);
}

/* =========================================================
   EXEC whitelist
   Only these commands are allowed:
       DATE
       UPTIME
       DISKFREE
       HOSTNAME
       WHOAMI
   ========================================================= */

void handle_exec(
    int client_fd,
    const char *command
)
{
    const char *system_command = NULL;

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

    FILE *process = popen(
        system_command,
        "r"
    );

    if (process == NULL)
    {
        send_response(
            client_fd,
            "ERR 004 EXEC_FAILED SID:" SESSION_ID "\n"
        );

        return;
    }

    char output[BUFFER_SIZE];

    size_t total = 0;

    while (
        total < sizeof(output) - 1 &&
        fgets(
            output + total,
            sizeof(output) - total,
            process
        ) != NULL
    )
    {
        total = strlen(output);
    }

    pclose(process);

    output[sizeof(output) - 1] = '\0';

    output[strcspn(output, "\r\n")] = '\0';

    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK EXEC_RESULT %s SID:%s\n",
        output,
        SESSION_ID
    );

    send_response(client_fd, response);
}

/* =========================================================
   PUT
   Controller sends:
       PUT filename filesize
   followed by raw file bytes
   ========================================================= */

void handle_put(
    int client_fd,
    const char *filename,
    unsigned long long file_size
)
{
    if (!valid_filename(filename))
    {
        send_response(
            client_fd,
            "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID "\n"
        );

        return;
    }

    /* Limit upload size to 100 MB */

    if (file_size > 100ULL * 1024ULL * 1024ULL)
    {
        send_response(
            client_fd,
            "ERR 006 FILE_TOO_LARGE SID:" SESSION_ID "\n"
        );

        return;
    }

    mkdir("./agentfiles", 0755);

    mkdir(STORAGE_DIR, 0755);

    char filepath[512];

    snprintf(
        filepath,
        sizeof(filepath),
        "%s/%s",
        STORAGE_DIR,
        filename
    );

    FILE *file = fopen(
        filepath,
        "wb"
    );

    if (file == NULL)
    {
        send_response(
            client_fd,
            "ERR 004 FILE_OPEN_FAILED SID:" SESSION_ID "\n"
        );

        return;
    }

    char buffer[FILE_BUFFER_SIZE];

    unsigned long long remaining = file_size;

    while (remaining > 0)
    {
        size_t chunk_size =
            remaining > FILE_BUFFER_SIZE
                ? FILE_BUFFER_SIZE
                : (size_t)remaining;

        ssize_t received = recv(
            client_fd,
            buffer,
            chunk_size,
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

    send_response(
        client_fd,
        response
    );

    printf(
        "File received: %s (%llu bytes)\n",
        filename,
        file_size
    );
}

/* =========================================================
   GET
   Agent sends:
       OK FILE_SEND filename filesize SID:8472
   followed by raw file bytes
   ========================================================= */

void handle_get(
    int client_fd,
    const char *filename
)
{
    if (!valid_filename(filename))
    {
        send_response(
            client_fd,
            "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID "\n"
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

    FILE *file = fopen(
        filepath,
        "rb"
    );

    if (file == NULL)
    {
        send_response(
            client_fd,
            "ERR 003 FILE_NOT_FOUND SID:" SESSION_ID "\n"
        );

        return;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);

        send_response(
            client_fd,
            "ERR 004 FILE_ERROR SID:" SESSION_ID "\n"
        );

        return;
    }

    long file_size = ftell(file);

    if (file_size < 0)
    {
        fclose(file);

        send_response(
            client_fd,
            "ERR 004 FILE_ERROR SID:" SESSION_ID "\n"
        );

        return;
    }

    rewind(file);

    char header[BUFFER_SIZE];

    snprintf(
        header,
        sizeof(header),
        "OK FILE_SEND %s %ld SID:%s\n",
        filename,
        file_size,
        SESSION_ID
    );

    if (send_all(
            client_fd,
            header,
            strlen(header)
        ) < 0)
    {
        fclose(file);
        return;
    }

    char buffer[FILE_BUFFER_SIZE];

    long remaining = file_size;

    while (remaining > 0)
    {
        size_t chunk_size =
            remaining > FILE_BUFFER_SIZE
                ? FILE_BUFFER_SIZE
                : (size_t)remaining;

        size_t bytes_read = fread(
            buffer,
            1,
            chunk_size,
            file
        );

        if (bytes_read == 0)
        {
            fclose(file);
            return;
        }

        if (send_all(
                client_fd,
                buffer,
                bytes_read
            ) < 0)
        {
            fclose(file);
            return;
        }

        remaining -= (long)bytes_read;
    }

    fclose(file);

    printf(
        "File sent: %s (%ld bytes)\n",
        filename,
        file_size
    );
}

/* =========================================================
   UDP MONITORING
   ========================================================= */

typedef struct
{
    int running;
    int udp_socket;

    unsigned short udp_port;

    struct sockaddr_in controller_address;

    pthread_t thread;

} MonitorContext;

/* =========================================================
   Monitor thread
   ========================================================= */

void *monitor_thread(void *arg)
{
    MonitorContext *monitor =
        (MonitorContext *)arg;

    while (monitor->running)
    {
        FILE *file;

        double cpu_load = 0.0;
        long memory_used_mb = 0;
        long uptime_seconds = 0;

        /* CPU */

        file = fopen(
            "/proc/loadavg",
            "r"
        );

        if (file != NULL)
        {
            fscanf(
                file,
                "%lf",
                &cpu_load
            );

            fclose(file);
        }

        /* Memory */

        file = fopen(
            "/proc/meminfo",
            "r"
        );

        if (file != NULL)
        {
            long mem_total = 0;
            long mem_available = 0;

            char line[256];

            while (fgets(
                line,
                sizeof(line),
                file
            ))
            {
                if (sscanf(
                    line,
                    "MemTotal: %ld kB",
                    &mem_total
                ) == 1)
                {
                    continue;
                }

                if (sscanf(
                    line,
                    "MemAvailable: %ld kB",
                    &mem_available
                ) == 1)
                {
                    continue;
                }
            }

            fclose(file);

            if (mem_total > 0)
            {
                memory_used_mb =
                    (mem_total - mem_available) / 1024;
            }
        }

        /* Uptime */

        file = fopen(
            "/proc/uptime",
            "r"
        );

        if (file != NULL)
        {
            double uptime;

            if (fscanf(
                file,
                "%lf",
                &uptime
            ) == 1)
            {
                uptime_seconds =
                    (long)uptime;
            }

            fclose(file);
        }

        char message[BUFFER_SIZE];

        snprintf(
            message,
            sizeof(message),
            "SYSINFO %.2f %ld %ld SID:%s",
            cpu_load,
            memory_used_mb,
            uptime_seconds,
            SESSION_ID
        );

        sendto(
            monitor->udp_socket,
            message,
            strlen(message),
            0,
            (struct sockaddr *)&monitor->controller_address,
            sizeof(monitor->controller_address)
        );

        sleep(MONITOR_INTERVAL);
    }

    return NULL;
}

/* =========================================================
   Start UDP monitoring
   ========================================================= */

void start_monitor(
    int client_fd,
    MonitorContext *monitor,
    unsigned short udp_port
)
{
    if (monitor->running)
    {
        send_response(
            client_fd,
            "ERR 007 MONITOR_ALREADY_RUNNING SID:" SESSION_ID "\n"
        );

        return;
    }

    monitor->udp_socket = socket(
        AF_INET,
        SOCK_DGRAM,
        0
    );

    if (monitor->udp_socket < 0)
    {
        send_response(
            client_fd,
            "ERR 004 UDP_SOCKET_FAILED SID:" SESSION_ID "\n"
        );

        return;
    }

    memset(
        &monitor->controller_address,
        0,
        sizeof(monitor->controller_address)
    );

    struct sockaddr_in peer_address;

    socklen_t peer_length =
        sizeof(peer_address);

    if (getpeername(
            client_fd,
            (struct sockaddr *)&peer_address,
            &peer_length
        ) < 0)
    {
        close(monitor->udp_socket);

        monitor->udp_socket = -1;

        send_response(
            client_fd,
            "ERR 004 PEER_ADDRESS_FAILED SID:" SESSION_ID "\n"
        );

        return;
    }

    monitor->controller_address.sin_family =
        AF_INET;

    monitor->controller_address.sin_addr =
        peer_address.sin_addr;

    monitor->controller_address.sin_port =
        htons(udp_port);

    monitor->udp_port = udp_port;

    monitor->running = 1;

    if (pthread_create(
            &monitor->thread,
            NULL,
            monitor_thread,
            monitor
        ) != 0)
    {
        monitor->running = 0;

        close(monitor->udp_socket);

        monitor->udp_socket = -1;

        send_response(
            client_fd,
            "ERR 004 MONITOR_THREAD_FAILED SID:" SESSION_ID "\n"
        );

        return;
    }

    send_response(
        client_fd,
        "OK MONITOR_STARTED SID:" SESSION_ID "\n"
    );
}

/* =========================================================
   Stop UDP monitoring
   ========================================================= */

void stop_monitor(
    int client_fd,
    MonitorContext *monitor
)
{
    if (!monitor->running)
    {
        send_response(
            client_fd,
            "ERR 008 MONITOR_NOT_RUNNING SID:" SESSION_ID "\n"
        );

        return;
    }

    monitor->running = 0;

    pthread_join(
        monitor->thread,
        NULL
    );

    close(
        monitor->udp_socket
    );

    monitor->udp_socket = -1;

    send_response(
        client_fd,
        "OK MONITOR_STOPPED SID:" SESSION_ID "\n"
    );
}

/* =========================================================
   Handle Controller
   ========================================================= */

void *handle_controller(void *arg)
{
    int client_fd = *(int *)arg;

    free(arg);

    int authenticated = 0;

    char command[BUFFER_SIZE];

    MonitorContext monitor;

    memset(
        &monitor,
        0,
        sizeof(monitor)
    );

    monitor.udp_socket = -1;

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

        /* =================================================
           AUTH
           ================================================= */

        if (strncmp(
                command,
                "AUTH ",
                5
            ) == 0)
        {
            const char *token =
                command + 5;

            if (strcmp(
                    token,
                    AUTH_TOKEN
                ) == 0)
            {
                authenticated = 1;

                printf(
                    "Controller authenticated.\n"
                );

                send_response(
                    client_fd,
                    "OK AUTHENTICATED SID:" SESSION_ID "\n"
                );
            }
            else
            {
                send_response(
                    client_fd,
                    "ERR 001 AUTH_FAILED SID:" SESSION_ID "\n"
                );
            }

            continue;
        }

        /* =================================================
           Authentication required
           ================================================= */

        if (!authenticated)
        {
            send_response(
                client_fd,
                "ERR 001 AUTH_REQUIRED SID:" SESSION_ID "\n"
            );

            continue;
        }

        /* =================================================
           SYSINFO
           ================================================= */

        if (strcmp(
                command,
                "SYSINFO"
            ) == 0)
        {
            handle_sysinfo(
                client_fd
            );
        }

        /* =================================================
           LISTPROC
           ================================================= */

        else if (strcmp(
                     command,
                     "LISTPROC"
                 ) == 0)
        {
            handle_listproc(
                client_fd
            );
        }

        /* =================================================
           EXEC
           ================================================= */

        else if (strncmp(
                     command,
                     "EXEC ",
                     5
                 ) == 0)
        {
            handle_exec(
                client_fd,
                command
            );
        }

        /* =================================================
           PUT
           ================================================= */

        else if (strncmp(
                     command,
                     "PUT ",
                     4
                 ) == 0)
        {
            char filename[256];

            unsigned long long file_size;

            if (sscanf(
                    command,
                    "PUT %255s %llu",
                    filename,
                    &file_size
                ) == 2)
            {
                handle_put(
                    client_fd,
                    filename,
                    file_size
                );
            }
            else
            {
                send_response(
                    client_fd,
                    "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID "\n"
                );
            }
        }

        /* =================================================
           GET
           ================================================= */

        else if (strncmp(
                     command,
                     "GET ",
                     4
                 ) == 0)
        {
            char filename[256];

            if (sscanf(
                    command,
                    "GET %255s",
                    filename
                ) == 1)
            {
                handle_get(
                    client_fd,
                    filename
                );
            }
            else
            {
                send_response(
                    client_fd,
                    "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID "\n"
                );
            }
        }

        /* =================================================
           MONITOR START
           ================================================= */

        else if (strncmp(
                     command,
                     "MONITOR START ",
                     14
                 ) == 0)
        {
            unsigned int port;

            if (sscanf(
                    command,
                    "MONITOR START %u",
                    &port
                ) == 1 &&
                port > 0 &&
                port <= 65535)
            {
                start_monitor(
                    client_fd,
                    &monitor,
                    (unsigned short)port
                );
            }
            else
            {
                send_response(
                    client_fd,
                    "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID "\n"
                );
            }
        }

        /* =================================================
           MONITOR STOP
           ================================================= */

        else if (strcmp(
                     command,
                     "MONITOR STOP"
                 ) == 0)
        {
            stop_monitor(
                client_fd,
                &monitor
            );
        }

        /* =================================================
           EXIT
           ================================================= */

        else if (strcmp(
                     command,
                     "EXIT"
                 ) == 0)
        {
            send_response(
                client_fd,
                "OK BYE SID:" SESSION_ID "\n"
            );

            break;
        }

        /* =================================================
           Unknown command
           ================================================= */

        else
        {
            send_response(
                client_fd,
                "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID "\n"
            );
        }
    }

    /* =====================================================
       Clean up monitor if Controller disconnects
       ===================================================== */

    if (monitor.running)
    {
        monitor.running = 0;

        pthread_join(
            monitor.thread,
            NULL
        );
    }

    if (monitor.udp_socket >= 0)
    {
        close(
            monitor.udp_socket
        );
    }

    close(client_fd);

    printf(
        "Controller disconnected.\n"
    );

    return NULL;
}

/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    int server_fd;

    struct sockaddr_in server_address;

    printf(
        "Agent starting...\n"
    );

    printf(
        "Port: %d\n",
        PORT
    );

    /* Create TCP socket */

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

    /* Allow address reuse */

    int option = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &option,
            sizeof(option)
        ) < 0)
    {
        perror("setsockopt");

        close(server_fd);

        return 1;
    }

    /* Configure server address */

    memset(
        &server_address,
        0,
        sizeof(server_address)
    );

    server_address.sin_family =
        AF_INET;

    server_address.sin_addr.s_addr =
        INADDR_ANY;

    server_address.sin_port =
        htons(PORT);

    /* Bind */

    if (bind(
            server_fd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)
        ) < 0)
    {
        perror("bind");

        close(server_fd);

        return 1;
    }

    /* Listen */

    if (listen(
            server_fd,
            BACKLOG
        ) < 0)
    {
        perror("listen");

        close(server_fd);

        return 1;
    }

    printf(
        "Listening...\n"
    );

    /* =====================================================
       Accept Controllers
       ===================================================== */

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

        printf(
            "Controller connected.\n"
        );

        int *client_socket =
            malloc(sizeof(int));

        if (client_socket == NULL)
        {
            close(client_fd);

            continue;
        }

        *client_socket = client_fd;

        pthread_t thread;

        if (pthread_create(
                &thread,
                NULL,
                handle_controller,
                client_socket
            ) != 0)
        {
            perror("pthread_create");

            close(client_fd);

            free(client_socket);

            continue;
        }

        pthread_detach(thread);
    }

    close(server_fd);

    return 0;
}
