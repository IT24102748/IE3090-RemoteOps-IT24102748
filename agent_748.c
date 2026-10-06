#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9410
#define BACKLOG 10

#define AUTH_TOKEN "OPS-2748"
#define SESSION_ID "8472"

#define BUFFER_SIZE 4096


/*
 * Send a complete response to the Controller.
 */
void send_response(int client_fd, const char *response)
{
    send(client_fd, response, strlen(response), 0);
}


/*
 * Get CPU load from /proc/loadavg
 */
double get_cpu_load(void)
{
    FILE *file;
    double load;

    file = fopen("/proc/loadavg", "r");

    if (file == NULL)
    {
        return -1.0;
    }

    if (fscanf(file, "%lf", &load) != 1)
    {
        fclose(file);
        return -1.0;
    }

    fclose(file);

    return load;
}


/*
 * Get used memory in MB.
 */
long get_memory_used_mb(void)
{
    FILE *file;

    char line[256];

    long mem_total = 0;
    long mem_available = 0;

    file = fopen("/proc/meminfo", "r");

    if (file == NULL)
    {
        return -1;
    }

    while (fgets(line, sizeof(line), file))
    {
        if (sscanf(line, "MemTotal: %ld kB",
                   &mem_total) == 1)
        {
            continue;
        }

        if (sscanf(line, "MemAvailable: %ld kB",
                   &mem_available) == 1)
        {
            continue;
        }
    }

    fclose(file);

    if (mem_total == 0)
    {
        return -1;
    }

    /*
     * Convert kB to MB.
     */
    long used_kb = mem_total - mem_available;

    return used_kb / 1024;
}


/*
 * Get system uptime in seconds.
 */
long get_uptime_seconds(void)
{
    FILE *file;

    double uptime;

    file = fopen("/proc/uptime", "r");

    if (file == NULL)
    {
        return -1;
    }

    if (fscanf(file, "%lf", &uptime) != 1)
    {
        fclose(file);
        return -1;
    }

    fclose(file);

    return (long)uptime;
}


/*
 * Handle SYSINFO command.
 */
void handle_sysinfo(int client_fd)
{
    double cpu_load;
    long mem_used_mb;
    long uptime_sec;

    char response[BUFFER_SIZE];

    cpu_load = get_cpu_load();

    mem_used_mb = get_memory_used_mb();

    uptime_sec = get_uptime_seconds();

    if (cpu_load < 0 ||
        mem_used_mb < 0 ||
        uptime_sec < 0)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 003 SYSINFO_FAILED SID:%s\n",
                 SESSION_ID);

        send_response(client_fd, response);

        return;
    }

    snprintf(response,
             sizeof(response),
             "OK SYSINFO %.2f %ld %ld SID:%s\n",
             cpu_load,
             mem_used_mb,
             uptime_sec,
             SESSION_ID);

    send_response(client_fd, response);
}


/*
 * Handle LISTPROC command.
 */
void handle_listproc(int client_fd)
{
    FILE *processes;

    char line[1024];

    char response[BUFFER_SIZE];

    size_t used = 0;


    memset(response, 0, sizeof(response));


    /*
     * Use ps to obtain running processes.
     */
    processes = popen("ps -eo pid,comm --no-headers",
                      "r");

    if (processes == NULL)
    {
        snprintf(response,
                 sizeof(response),
                 "ERR 004 LISTPROC_FAILED SID:%s\n",
                 SESSION_ID);

        send_response(client_fd, response);

        return;
    }


    /*
     * Start protocol response.
     */
    used = snprintf(response,
                    sizeof(response),
                    "OK PROCS ");


    /*
     * Read process information.
     */
    while (fgets(line, sizeof(line), processes) != NULL)
    {
        size_t line_length;

        line_length = strlen(line);

        /*
         * Avoid overflowing the response buffer.
         */
        if (used + line_length + 20 >= sizeof(response))
        {
            break;
        }

        /*
         * Remove newline.
         */
        line[strcspn(line, "\r\n")] = '\0';


        used += snprintf(response + used,
                         sizeof(response) - used,
                         "[%s] ",
                         line);
    }


    pclose(processes);


    /*
     * Add session ID.
     */
    snprintf(response + used,
             sizeof(response) - used,
             "SID:%s\n",
             SESSION_ID);


    send_response(client_fd, response);
}


/*
 * Handle one Controller connection.
 */
void *handle_controller(void *arg)
{
    int client_fd;

    char buffer[BUFFER_SIZE];

    int authenticated = 0;


    client_fd = *(int *)arg;

    free(arg);


    printf("Controller connected. Thread ID: %lu\n",
           (unsigned long)pthread_self());


    while (1)
    {
        memset(buffer, 0, sizeof(buffer));


        ssize_t bytes_received;

        bytes_received = recv(client_fd,
                              buffer,
                              sizeof(buffer) - 1,
                              0);


        if (bytes_received <= 0)
        {
            printf("Controller disconnected. Thread ID: %lu\n",
                   (unsigned long)pthread_self());

            break;
        }


        buffer[bytes_received] = '\0';


        /*
         * Remove CR/LF.
         */
        buffer[strcspn(buffer, "\r\n")] = '\0';


        printf("Received: %s\n", buffer);


        /*
         * AUTH
         */
        if (strncmp(buffer, "AUTH ", 5) == 0)
        {
            char token[100];


            if (sscanf(buffer,
                       "AUTH %99s",
                       token) == 1)
            {
                if (strcmp(token, AUTH_TOKEN) == 0)
                {
                    authenticated = 1;

                    send_response(
                        client_fd,
                        "OK AUTHENTICATED SID:8472\n"
                    );

                    printf("Authentication successful.\n");
                }
                else
                {
                    authenticated = 0;

                    send_response(
                        client_fd,
                        "ERR 001 AUTH_FAILED SID:8472\n"
                    );

                    printf("Authentication failed.\n");
                }
            }
            else
            {
                send_response(
                    client_fd,
                    "ERR 001 AUTH_FAILED SID:8472\n"
                );
            }

            continue;
        }


        /*
         * No command is allowed before AUTH.
         */
        if (!authenticated)
        {
            send_response(
                client_fd,
                "ERR 001 AUTH_FAILED SID:8472\n"
            );

            continue;
        }


        /*
         * SYSINFO
         */
        if (strcmp(buffer, "SYSINFO") == 0)
        {
            handle_sysinfo(client_fd);

            continue;
        }


        /*
         * LISTPROC
         */
        if (strcmp(buffer, "LISTPROC") == 0)
        {
            handle_listproc(client_fd);

            continue;
        }


        /*
         * Unknown command.
         */
        send_response(
            client_fd,
            "ERR 002 COMMAND_NOT_ALLOWED SID:8472\n"
        );
    }


    close(client_fd);

    return NULL;
}


int main(void)
{
    int server_fd;

    struct sockaddr_in server_addr;

    struct sockaddr_in client_addr;

    socklen_t client_len;

    int opt = 1;


    printf("Agent starting...\n");
    printf("Port: %d\n", PORT);


    /*
     * Create TCP socket.
     */
    server_fd = socket(AF_INET,
                       SOCK_STREAM,
                       0);


    if (server_fd == -1)
    {
        perror("socket");

        exit(EXIT_FAILURE);
    }


    /*
     * Allow address reuse.
     */
    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt)) == -1)
    {
        perror("setsockopt");

        close(server_fd);

        exit(EXIT_FAILURE);
    }


    memset(&server_addr,
           0,
           sizeof(server_addr));


    server_addr.sin_family = AF_INET;

    server_addr.sin_port = htons(PORT);

    server_addr.sin_addr.s_addr = INADDR_ANY;


    /*
     * Bind.
     */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1)
    {
        perror("bind");

        close(server_fd);

        exit(EXIT_FAILURE);
    }


    /*
     * Listen.
     */
    if (listen(server_fd,
               BACKLOG) == -1)
    {
        perror("listen");

        close(server_fd);

        exit(EXIT_FAILURE);
    }


    printf("Listening...\n");


    /*
     * Accept Controllers continuously.
     */
    while (1)
    {
        int *client_fd;

        pthread_t thread_id;


        client_fd = malloc(sizeof(int));


        if (client_fd == NULL)
        {
            perror("malloc");

            continue;
        }


        client_len = sizeof(client_addr);


        *client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_addr,
            &client_len
        );


        if (*client_fd == -1)
        {
            perror("accept");

            free(client_fd);

            continue;
        }


        /*
         * Create a thread for this Controller.
         */
        if (pthread_create(&thread_id,
                           NULL,
                           handle_controller,
                           client_fd) != 0)
        {
            perror("pthread_create");

            close(*client_fd);

            free(client_fd);

            continue;
        }


        pthread_detach(thread_id);
    }


    close(server_fd);

    return 0;
}
