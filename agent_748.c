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

#define BUFFER_SIZE 1024


void send_response(int client_fd, const char *response)
{
    send(client_fd, response, strlen(response), 0);
}


void *handle_controller(void *arg)
{
    int client_fd = *(int *)arg;

    free(arg);

    char buffer[BUFFER_SIZE];

    int authenticated = 0;

    printf("Controller connected. Thread ID: %lu\n",
           (unsigned long)pthread_self());

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        ssize_t bytes_received = recv(client_fd,
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

        /* Remove newline characters */
        buffer[strcspn(buffer, "\r\n")] = '\0';

        printf("Received: %s\n", buffer);


        /*
         * AUTHENTICATION
         */
        if (strncmp(buffer, "AUTH ", 5) == 0)
        {
            char token[100];

            if (sscanf(buffer, "AUTH %99s", token) == 1)
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
         * REJECT ALL OTHER COMMANDS BEFORE AUTH
         */
        if (!authenticated)
        {
            send_response(
                client_fd,
                "ERR 001 AUTH_FAILED SID:8472\n"
            );

            printf("Command rejected: Controller not authenticated.\n");

            continue;
        }


        /*
         * Commands after successful authentication
         *
         * More assignment commands will be added here later.
         */
        send_response(
            client_fd,
            "OK COMMAND RECEIVED\n"
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

    socklen_t client_len = sizeof(client_addr);

    int opt = 1;

    printf("Agent starting...\n");
    printf("Port: %d\n", PORT);


    /* Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }


    /* Allow address reuse */
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


    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;


    /* Bind */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1)
    {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }


    /* Listen */
    if (listen(server_fd, BACKLOG) == -1)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }


    printf("Listening...\n");


    /* Accept multiple Controllers */
    while (1)
    {
        int *client_fd = malloc(sizeof(int));

        if (client_fd == NULL)
        {
            perror("malloc");
            continue;
        }


        *client_fd = accept(server_fd,
                            (struct sockaddr *)&client_addr,
                            &client_len);

        if (*client_fd == -1)
        {
            perror("accept");
            free(client_fd);
            continue;
        }


        pthread_t thread_id;


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
