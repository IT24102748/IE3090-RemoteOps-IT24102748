#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

/* ============================================================
   CONFIGURATION
   ============================================================ */

#define PORT 9410
#define BACKLOG 10

#define AUTH_TOKEN "OPS-2748"
#define SESSION_ID "8472"

#define BUFFER_SIZE 8192
#define FILE_BUFFER_SIZE 4096

#define STORAGE_DIR "./agentfiles/IT24102748"

#define LOG_FILE "remoteops_IT24102748.log"

#define MONITOR_INTERVAL 2

/* ============================================================
   GLOBAL LOGGING LOCK
   ============================================================ */

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================
   MONITOR STRUCTURE
   ============================================================ */

typedef struct
{
    int running;
    int udp_socket;
    unsigned short udp_port;

    struct sockaddr_in controller_address;

    pthread_t thread;

} MonitorContext;

/* ============================================================
   FUNCTION PROTOTYPES
   ============================================================ */

void *handle_controller(void *arg);

void handle_sysinfo(
    int client_fd
);

void handle_listproc(
    int client_fd
);

void handle_exec(
    int client_fd,
    const char *command
);

void handle_put(
    int client_fd,
    const char *filename,
    unsigned long long file_size
);

void handle_get(
    int client_fd,
    const char *filename
);

void handle_monitor_start(
    int client_fd,
    MonitorContext *monitor,
    unsigned short udp_port
);

void handle_monitor_stop(
    int client_fd,
    MonitorContext *monitor
);

void *monitor_thread_function(
    void *arg
);

void stop_monitor_internal(
    MonitorContext *monitor
);

void write_log(
    const char *event
);

/* ============================================================
   SEND ALL
   ============================================================ */

/*
 * TCP send() is not guaranteed to send all requested bytes.
 *
 * This function keeps calling send() until every byte has
 * been transmitted.
 */

int send_all(
    int socket_fd,
    const void *buffer,
    size_t length
)
{
    size_t total_sent = 0;

    const char *data =
        (const char *)buffer;

    while (total_sent < length)
    {
        ssize_t sent = send(
            socket_fd,
            data + total_sent,
            length - total_sent,
            0
        );

        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (sent == 0)
        {
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

/* ============================================================
   RECEIVE ALL
   ============================================================ */

/*
 * Receive exactly 'length' bytes.
 *
 * This is useful for fixed-size file data.
 */

int recv_all(
    int socket_fd,
    void *buffer,
    size_t length
)
{
    size_t total_received = 0;

    char *data =
        (char *)buffer;

    while (total_received < length)
    {
        ssize_t received = recv(
            socket_fd,
            data + total_received,
            length - total_received,
            0
        );

        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (received == 0)
        {
            return -1;
        }

        total_received += (size_t)received;
    }

    return 0;
}

/* ============================================================
   RECEIVE ONE COMPLETE LINE
   ============================================================ */

/*
 * TCP is a byte stream.
 *
 * This function does NOT assume that one recv() contains
 * one complete command.
 *
 * It continues receiving bytes until '\n' is found.
 */

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

        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (received == 0)
        {
            return 0;
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

    return 1;
}

/* ============================================================
   SEND RESPONSE
   ============================================================ */

void send_response(
    int client_fd,
    const char *response
)
{
    char message[BUFFER_SIZE];

    snprintf(
        message,
        sizeof(message),
        "%s\n",
        response
    );

    send_all(
        client_fd,
        message,
        strlen(message)
    );
}

/* ============================================================
   LOGGING
   ============================================================ */

void write_log(
    const char *event
)
{
    FILE *log_file;

    time_t current_time;

    struct tm time_info;

    char timestamp[64];

    current_time = time(NULL);

    if (localtime_r(
            &current_time,
            &time_info) == NULL)
    {
        return;
    }

    strftime(
        timestamp,
        sizeof(timestamp),
        "%Y-%m-%d %H:%M:%S",
        &time_info
    );

    pthread_mutex_lock(
        &log_mutex
    );

    log_file = fopen(
        LOG_FILE,
        "a"
    );

    if (log_file != NULL)
    {
        fprintf(
            log_file,
            "[%s] %s\n",
            timestamp,
            event
        );

        fflush(log_file);

        fclose(log_file);
    }

    pthread_mutex_unlock(
        &log_mutex
    );
}

/* ============================================================
   VALIDATE FILE NAME
   ============================================================ */

/*
 * Prevent path traversal such as:
 *
 * ../file
 * /etc/passwd
 * ../../something
 */

int valid_filename(
    const char *filename
)
{
    if (filename == NULL)
    {
        return 0;
    }

    if (strlen(filename) == 0)
    {
        return 0;
    }

    if (strlen(filename) >= 256)
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

/* ============================================================
   SYSINFO
   ============================================================ */

void handle_sysinfo(
    int client_fd
)
{
    FILE *load_file;

    double load_average = 0.0;

    long uptime_seconds = 0;

    char buffer[BUFFER_SIZE];

    /*
     * Read /proc/loadavg
     */

    load_file = fopen(
        "/proc/loadavg",
        "r"
    );

    if (load_file != NULL)
    {
        fscanf(
            load_file,
            "%lf",
            &load_average
        );

        fclose(load_file);
    }

    /*
     * Read /proc/uptime
     */

    load_file = fopen(
        "/proc/uptime",
        "r"
    );

    if (load_file != NULL)
    {
        double uptime_value = 0.0;

        fscanf(
            load_file,
            "%lf",
            &uptime_value
        );

        uptime_seconds =
            (long)uptime_value;

        fclose(load_file);
    }

    snprintf(
        buffer,
        sizeof(buffer),
        "OK SYSINFO LOAD:%.2f UPTIME:%ld SID:%s",
        load_average,
        uptime_seconds,
        SESSION_ID
    );

    send_response(
        client_fd,
        buffer
    );
}

/* ============================================================
   LIST PROCESS
   ============================================================ */

void handle_listproc(
    int client_fd
)
{
    FILE *process_pipe;

    char buffer[BUFFER_SIZE];

    char response[BUFFER_SIZE];

    process_pipe = popen(
        "ps -e -o pid,comm --no-headers | head -20",
        "r"
    );

    if (process_pipe == NULL)
    {
        send_response(
            client_fd,
            "ERR 001 INTERNAL_ERROR SID:" SESSION_ID
        );

        return;
    }

    snprintf(
        response,
        sizeof(response),
        "OK LISTPROC SID:%s\n",
        SESSION_ID
    );

    send_all(
        client_fd,
        response,
        strlen(response)
    );

    while (fgets(
        buffer,
        sizeof(buffer),
        process_pipe) != NULL)
    {
        send_all(
            client_fd,
            buffer,
            strlen(buffer)
        );
    }

    pclose(process_pipe);

    /*
     * End marker.
     */

    send_response(
        client_fd,
        "END LISTPROC"
    );
}

/* ============================================================
   EXEC WHITELIST
   ============================================================ */

void handle_exec(
    int client_fd,
    const char *command
)
{
    const char *system_command = NULL;

    char response[BUFFER_SIZE];

    /*
     * Only explicitly approved commands are allowed.
     */

    if (strcmp(
            command,
            "EXEC DATE") == 0)
    {
        system_command = "date";
    }
    else if (strcmp(
                 command,
                 "EXEC UPTIME") == 0)
    {
        system_command = "uptime";
    }
    else if (strcmp(
                 command,
                 "EXEC DISKFREE") == 0)
    {
        system_command = "df -h .";
    }
    else if (strcmp(
                 command,
                 "EXEC HOSTNAME") == 0)
    {
        system_command = "hostname";
    }
    else if (strcmp(
                 command,
                 "EXEC WHOAMI") == 0)
    {
        system_command = "whoami";
    }
    else
    {
        snprintf(
            response,
            sizeof(response),
            "ERR 002 COMMAND_NOT_ALLOWED SID:%s",
            SESSION_ID
        );

        send_response(
            client_fd,
            response
        );

        return;
    }

    FILE *command_pipe = popen(
        system_command,
        "r"
    );

    if (command_pipe == NULL)
    {
        send_response(
            client_fd,
            "ERR 001 INTERNAL_ERROR SID:" SESSION_ID
        );

        return;
    }

    snprintf(
        response,
        sizeof(response),
        "OK EXEC SID:%s",
        SESSION_ID
    );

    send_response(
        client_fd,
        response
    );

    char output[BUFFER_SIZE];

    while (fgets(
        output,
        sizeof(output),
        command_pipe) != NULL)
    {
        send_all(
            client_fd,
            output,
            strlen(output)
        );
    }

    pclose(command_pipe);

    send_response(
        client_fd,
        "END EXEC"
    );
}

/* ============================================================
   PUT FILE
   ============================================================ */

void handle_put(
    int client_fd,
    const char *filename,
    unsigned long long file_size
)
{
    char path[512];

    FILE *file;

    char buffer[FILE_BUFFER_SIZE];

    unsigned long long total_received = 0;

    /*
     * Validate filename.
     */

    if (!valid_filename(filename))
    {
        send_response(
            client_fd,
            "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID
        );

        return;
    }

    /*
     * Maximum file size:
     * 100 MB
     */

    if (file_size > 100ULL * 1024ULL * 1024ULL)
    {
        send_response(
            client_fd,
            "ERR 001 FILE_TOO_LARGE SID:" SESSION_ID
        );

        return;
    }

    /*
     * Make sure personalized storage directory exists.
     */

    if (mkdir(
            "agentfiles",
            0755) < 0 &&
        errno != EEXIST)
    {
        perror("mkdir agentfiles");
    }

    if (mkdir(
            STORAGE_DIR,
            0755) < 0 &&
        errno != EEXIST)
    {
        perror("mkdir storage");
    }

    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        STORAGE_DIR,
        filename
    );

    file = fopen(
        path,
        "wb"
    );

    if (file == NULL)
    {
        perror("fopen PUT");

        send_response(
            client_fd,
            "ERR 001 FILE_OPEN_FAILED SID:" SESSION_ID
        );

        return;
    }

    /*
     * Receive EXACTLY file_size bytes.
     */

    while (total_received < file_size)
    {
        size_t bytes_to_receive =
            FILE_BUFFER_SIZE;

        if (file_size - total_received <
            bytes_to_receive)
        {
            bytes_to_receive =
                (size_t)(
                    file_size - total_received
                );
        }

        ssize_t received = recv(
            client_fd,
            buffer,
            bytes_to_receive,
            0
        );

        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("recv PUT");

            fclose(file);

            return;
        }

        if (received == 0)
        {
            printf(
                "Controller disconnected during PUT.\n"
            );

            fclose(file);

            return;
        }

        size_t written = fwrite(
            buffer,
            1,
            (size_t)received,
            file
        );

        if (written !=
            (size_t)received)
        {
            perror("fwrite PUT");

            fclose(file);

            return;
        }

        total_received +=
            (unsigned long long)received;
    }

    fclose(file);

    printf(
        "File received: %s (%llu bytes)\n",
        filename,
        total_received
    );

    char log_message[BUFFER_SIZE];

    snprintf(
        log_message,
        sizeof(log_message),
        "File upload completed: %s (%llu bytes)",
        filename,
        total_received
    );

    write_log(
        log_message
    );

    snprintf(
        log_message,
        sizeof(log_message),
        "OK FILE_RECEIVED %s SID:%s",
        filename,
        SESSION_ID
    );

    send_response(
        client_fd,
        log_message
    );
}

/* ============================================================
   GET FILE
   ============================================================ */

void handle_get(
    int client_fd,
    const char *filename
)
{
    char path[512];

    FILE *file;

    unsigned long long file_size;

    char buffer[FILE_BUFFER_SIZE];

    /*
     * Validate filename.
     */

    if (!valid_filename(filename))
    {
        send_response(
            client_fd,
            "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID
        );

        return;
    }

    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        STORAGE_DIR,
        filename
    );

    file = fopen(
        path,
        "rb"
    );

    if (file == NULL)
    {
        send_response(
            client_fd,
            "ERR 003 FILE_NOT_FOUND SID:" SESSION_ID
        );

        return;
    }

    /*
     * Find file size.
     */

    if (fseek(
            file,
            0,
            SEEK_END) != 0)
    {
        fclose(file);

        send_response(
            client_fd,
            "ERR 001 FILE_READ_FAILED SID:" SESSION_ID
        );

        return;
    }

    long size = ftell(file);

    if (size < 0)
    {
        fclose(file);

        send_response(
            client_fd,
            "ERR 001 FILE_READ_FAILED SID:" SESSION_ID
        );

        return;
    }

    file_size =
        (unsigned long long)size;

    rewind(file);

    /*
     * Send header first.
     */

    char header[BUFFER_SIZE];

    snprintf(
        header,
        sizeof(header),
        "OK FILE_SEND %s %llu SID:%s\n",
        filename,
        file_size,
        SESSION_ID
    );

    if (send_all(
            client_fd,
            header,
            strlen(header)) < 0)
    {
        fclose(file);

        return;
    }

    /*
     * Send exact file bytes.
     */

    unsigned long long total_sent = 0;

    while (total_sent < file_size)
    {
        size_t bytes_to_read =
            FILE_BUFFER_SIZE;

        if (file_size - total_sent <
            bytes_to_read)
        {
            bytes_to_read =
                (size_t)(
                    file_size - total_sent
                );
        }

        size_t bytes_read =
            fread(
                buffer,
                1,
                bytes_to_read,
                file
            );

        if (bytes_read == 0)
        {
            if (ferror(file))
            {
                perror("fread GET");
            }

            fclose(file);

            return;
        }

        if (send_all(
                client_fd,
                buffer,
                bytes_read) < 0)
        {
            fclose(file);

            return;
        }

        total_sent +=
            (unsigned long long)bytes_read;
    }

    fclose(file);

    printf(
        "File sent: %s (%llu bytes)\n",
        filename,
        file_size
    );

    char log_message[BUFFER_SIZE];

    snprintf(
        log_message,
        sizeof(log_message),
        "File download completed: %s (%llu bytes)",
        filename,
        file_size
    );

    write_log(
        log_message
    );
}

/* ============================================================
   MONITOR THREAD
   ============================================================ */

void *monitor_thread_function(
    void *arg
)
{
    MonitorContext *monitor =
        (MonitorContext *)arg;

    while (monitor->running)
    {
        /*
         * Generate simple system information.
         */

        double load_average = 0.0;

        long uptime_seconds = 0;

        FILE *file =
            fopen(
                "/proc/loadavg",
                "r"
            );

        if (file != NULL)
        {
            fscanf(
                file,
                "%lf",
                &load_average
            );

            fclose(file);
        }

        file =
            fopen(
                "/proc/uptime",
                "r"
            );

        if (file != NULL)
        {
            double uptime_value = 0.0;

            fscanf(
                file,
                "%lf",
                &uptime_value
            );

            uptime_seconds =
                (long)uptime_value;

            fclose(file);
        }

        char message[BUFFER_SIZE];

        snprintf(
            message,
            sizeof(message),
            "SYSINFO %.2f %ld %ld SID:%s",
            load_average,
            uptime_seconds,
            (long)time(NULL),
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

        /*
         * Sleep in smaller intervals so MONITOR STOP
         * can terminate reasonably quickly.
         */

        for (int i = 0;
             i < MONITOR_INTERVAL * 10 &&
             monitor->running;
             i++)
        {
            usleep(100000);
        }
    }

    return NULL;
}

/* ============================================================
   MONITOR START
   ============================================================ */

void handle_monitor_start(
    int client_fd,
    MonitorContext *monitor,
    unsigned short udp_port
)
{
    if (monitor->running)
    {
        send_response(
            client_fd,
            "ERR 001 MONITOR_ALREADY_RUNNING SID:" SESSION_ID
        );

        return;
    }

    /*
     * Get Controller IP from the TCP connection.
     */

    socklen_t address_length =
        sizeof(monitor->controller_address);

    memset(
        &monitor->controller_address,
        0,
        sizeof(monitor->controller_address)
    );

    if (getpeername(
            client_fd,
            (struct sockaddr *)&monitor->controller_address,
            &address_length) < 0)
    {
        perror("getpeername");

        send_response(
            client_fd,
            "ERR 001 MONITOR_FAILED SID:" SESSION_ID
        );

        return;
    }

    monitor->controller_address.sin_port =
        htons(udp_port);

    /*
     * Create UDP socket.
     */

    monitor->udp_socket = socket(
        AF_INET,
        SOCK_DGRAM,
        0
    );

    if (monitor->udp_socket < 0)
    {
        perror("UDP socket");

        send_response(
            client_fd,
            "ERR 001 MONITOR_FAILED SID:" SESSION_ID
        );

        return;
    }

    monitor->udp_port =
        udp_port;

    monitor->running = 1;

    /*
     * Start monitor thread.
     */

    if (pthread_create(
            &monitor->thread,
            NULL,
            monitor_thread_function,
            monitor) != 0)
    {
        perror("pthread_create");

        monitor->running = 0;

        close(
            monitor->udp_socket
        );

        monitor->udp_socket = -1;

        send_response(
            client_fd,
            "ERR 001 MONITOR_FAILED SID:" SESSION_ID
        );

        return;
    }

    printf(
        "UDP monitoring started on port %u.\n",
        udp_port
    );

    char log_message[BUFFER_SIZE];

    snprintf(
        log_message,
        sizeof(log_message),
        "UDP monitoring started on port %u",
        udp_port
    );

    write_log(
        log_message
    );

    send_response(
        client_fd,
        "OK MONITOR_STARTED SID:" SESSION_ID
    );
}

/* ============================================================
   STOP MONITOR INTERNAL
   ============================================================ */

void stop_monitor_internal(
    MonitorContext *monitor
)
{
    if (!monitor->running)
    {
        return;
    }

    monitor->running = 0;

    /*
     * Wait for monitor thread.
     */

    pthread_join(
        monitor->thread,
        NULL
    );

    /*
     * Close UDP socket.
     */

    if (monitor->udp_socket >= 0)
    {
        close(
            monitor->udp_socket
        );

        monitor->udp_socket = -1;
    }
}

/* ============================================================
   MONITOR STOP
   ============================================================ */

void handle_monitor_stop(
    int client_fd,
    MonitorContext *monitor
)
{
    if (!monitor->running)
    {
        send_response(
            client_fd,
            "ERR 001 MONITOR_NOT_RUNNING SID:" SESSION_ID
        );

        return;
    }

    stop_monitor_internal(
        monitor
    );

    printf(
        "UDP monitoring stopped.\n"
    );

    write_log(
        "UDP monitoring stopped"
    );

    send_response(
        client_fd,
        "OK MONITOR_STOPPED SID:" SESSION_ID
    );
}

/* ============================================================
   CONTROLLER HANDLER
   ============================================================ */

void *handle_controller(
    void *arg
)
{
    int client_fd =
        *((int *)arg);

    free(arg);

    /*
     * Each Controller gets its own monitor context.
     */

    MonitorContext monitor;

    memset(
        &monitor,
        0,
        sizeof(monitor)
    );

    monitor.udp_socket = -1;

    /*
     * Log connection.
     */

    write_log(
        "Controller connected"
    );

    printf(
        "Controller connected.\n"
    );

    int authenticated = 0;

    char command[BUFFER_SIZE];

    while (1)
    {
        int result =
            recv_line(
                client_fd,
                command,
                sizeof(command)
            );

        /*
         * Connection closed.
         */

        if (result == 0)
        {
            printf(
                "Controller disconnected.\n"
            );

            write_log(
                "Controller disconnected"
            );

            break;
        }

        /*
         * Receive error.
         */

        if (result < 0)
        {
            perror("recv_line");

            write_log(
                "Controller connection receive error"
            );

            break;
        }

        /*
         * Display command.
         */

        printf(
            "Received command: %s\n",
            command
        );

        /*
         * Log command.
         */

        char log_message[BUFFER_SIZE];

        snprintf(
            log_message,
            sizeof(log_message),
            "Command received: %s",
            command
        );

        write_log(
            log_message
        );

        /* ====================================================
           AUTH
           ==================================================== */

        if (strncmp(
                command,
                "AUTH ",
                5) == 0)
        {
            const char *token =
                command + 5;

            if (strcmp(
                    token,
                    AUTH_TOKEN) == 0)
            {
                authenticated = 1;

                printf(
                    "Controller authenticated.\n"
                );

                write_log(
                    "Controller authenticated"
                );

                send_response(
                    client_fd,
                    "OK AUTHENTICATED SID:" SESSION_ID
                );
            }
            else
            {
                write_log(
                    "Authentication failed"
                );

                send_response(
                    client_fd,
                    "ERR 004 AUTH_FAILED SID:" SESSION_ID
                );
            }

            continue;
        }

        /* ====================================================
           AUTH REQUIRED
           ==================================================== */

        if (!authenticated)
        {
            send_response(
                client_fd,
                "ERR 004 AUTH_REQUIRED SID:" SESSION_ID
            );

            continue;
        }

        /* ====================================================
           SYSINFO
           ==================================================== */

        if (strcmp(
                command,
                "SYSINFO") == 0)
        {
            handle_sysinfo(
                client_fd
            );

            continue;
        }

        /* ====================================================
           LISTPROC
           ==================================================== */

        if (strcmp(
                command,
                "LISTPROC") == 0)
        {
            handle_listproc(
                client_fd
            );

            continue;
        }

        /* ====================================================
           EXEC
           ==================================================== */

        if (strncmp(
                command,
                "EXEC ",
                5) == 0)
        {
            handle_exec(
                client_fd,
                command
            );

            continue;
        }

        /* ====================================================
           PUT
           ==================================================== */

        if (strncmp(
                command,
                "PUT ",
                4) == 0)
        {
            char filename[256];

            unsigned long long file_size;

            int parsed = sscanf(
                command,
                "PUT %255s %llu",
                filename,
                &file_size
            );

            if (parsed != 2)
            {
                send_response(
                    client_fd,
                    "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID
                );

                continue;
            }

            handle_put(
                client_fd,
                filename,
                file_size
            );

            continue;
        }

        /* ====================================================
           GET
           ==================================================== */

        if (strncmp(
                command,
                "GET ",
                4) == 0)
        {
            char filename[256];

            if (sscanf(
                    command,
                    "GET %255s",
                    filename) != 1)
            {
                send_response(
                    client_fd,
                    "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID
                );

                continue;
            }

            handle_get(
                client_fd,
                filename
            );

            continue;
        }

        /* ====================================================
           MONITOR START
           ==================================================== */

        if (strncmp(
                command,
                "MONITOR START ",
                14) == 0)
        {
            unsigned int udp_port;

            if (sscanf(
                    command + 14,
                    "%u",
                    &udp_port) != 1)
            {
                send_response(
                    client_fd,
                    "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID
                );

                continue;
            }

            if (udp_port < 1024 ||
                udp_port > 65535)
            {
                send_response(
                    client_fd,
                    "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID
                );

                continue;
            }

            handle_monitor_start(
                client_fd,
                &monitor,
                (unsigned short)udp_port
            );

            continue;
        }

        /* ====================================================
           MONITOR STOP
           ==================================================== */

        if (strcmp(
                command,
                "MONITOR STOP") == 0)
        {
            handle_monitor_stop(
                client_fd,
                &monitor
            );

            continue;
        }

        /* ====================================================
           QUIT
           ==================================================== */

        if (strcmp(
                command,
                "QUIT") == 0)
        {
            /*
             * Assignment requires monitoring to stop
             * before closing the connection.
             */

            if (monitor.running)
            {
                printf(
                    "Monitoring active. Stopping before QUIT.\n"
                );

                write_log(
                    "Monitoring stopped before QUIT"
                );

                stop_monitor_internal(
                    &monitor
                );
            }

            /*
             * Log QUIT.
             */

            write_log(
                "Controller requested QUIT"
            );

            /*
             * Send final response.
             */

            send_response(
                client_fd,
                "OK BYE SID:" SESSION_ID
            );

            printf(
                "QUIT acknowledged. Closing connection.\n"
            );

            break;
        }

        /* ====================================================
           OPTIONAL EXIT SUPPORT
           ==================================================== */

        /*
         * EXIT is retained as a compatibility command.
         * The assignment's official command is QUIT.
         */

        if (strcmp(
                command,
                "EXIT") == 0)
        {
            if (monitor.running)
            {
                stop_monitor_internal(
                    &monitor
                );

                write_log(
                    "Monitoring stopped before EXIT"
                );
            }

            write_log(
                "Controller requested EXIT"
            );

            send_response(
                client_fd,
                "OK BYE SID:" SESSION_ID
            );

            break;
        }

        /* ====================================================
           UNKNOWN COMMAND
           ==================================================== */

        send_response(
            client_fd,
            "ERR 002 COMMAND_NOT_ALLOWED SID:" SESSION_ID
        );
    }

    /*
     * Safety:
     *
     * If the Controller disconnected while monitoring
     * was still active, stop the monitor before closing
     * the connection.
     */

    if (monitor.running)
    {
        stop_monitor_internal(
            &monitor
        );

        write_log(
            "Monitoring stopped because Controller disconnected"
        );
    }

    /*
     * PART P:
     * Log the final disconnect.
     */

    write_log(
        "Controller disconnected"
    );

    close(
        client_fd
    );

    pthread_exit(
        NULL
    );

    return NULL;
}

/* ============================================================
   MAIN
   ============================================================ */

int main(void)
{
    int server_fd;

    struct sockaddr_in server_address;

    /*
     * Ignore SIGPIPE so that a disconnected Controller
     * does not terminate the entire Agent process.
     */

    signal(
        SIGPIPE,
        SIG_IGN
    );

    /*
     * Create TCP socket.
     */

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

    /*
     * Allow quick restart of Agent.
     */

    int reuse = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    /*
     * Configure server address.
     */

    memset(
        &server_address,
        0,
        sizeof(server_address)
    );

    server_address.sin_family =
        AF_INET;

    server_address.sin_addr.s_addr =
        htonl(INADDR_ANY);

    server_address.sin_port =
        htons(PORT);

    /*
     * Bind.
     */

    if (bind(
            server_fd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    /*
     * Listen.
     */

    if (listen(
            server_fd,
            BACKLOG) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    /*
     * Startup information.
     */

    printf(
        "Agent starting...\n"
    );

    printf(
        "Port: %d\n",
        PORT
    );

    printf(
        "Storage: %s\n",
        STORAGE_DIR
    );

    printf(
        "Log file: %s\n",
        LOG_FILE
    );

    printf(
        "Listening...\n"
    );

    write_log(
        "Agent started"
    );

    /*
     * Main accept loop.
     */

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
            if (errno == EINTR)
            {
                continue;
            }

            perror("accept");
            continue;
        }

        /*
         * Allocate socket descriptor for the thread.
         */

        int *client_socket =
            malloc(sizeof(int));

        if (client_socket == NULL)
        {
            perror("malloc");

            close(client_fd);

            continue;
        }

        *client_socket =
            client_fd;

        /*
         * Create Controller thread.
         */

        pthread_t thread;

        if (pthread_create(
                &thread,
                NULL,
                handle_controller,
                client_socket) != 0)
        {
            perror("pthread_create");

            close(client_fd);

            free(client_socket);

            continue;
        }

        /*
         * Detached thread:
         * resources are automatically released
         * when the thread finishes.
         */

        pthread_detach(
            thread
        );
    }

    close(
        server_fd
    );

    return 0;
}
