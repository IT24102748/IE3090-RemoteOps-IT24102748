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

void *handle_controller(void *arg)
{
    int client_fd = *(int *)arg;

    /* Free memory allocated for client socket */
    free(arg);

    printf("Controller connected. Thread ID: %lu\n",
           (unsigned long)pthread_self());

    /*
     * Keep this Controller connection open.
     * This allows us to test multiple simultaneous
     * Controller connections.
     */
    sleep(30);

    printf("Controller disconnected. Thread ID: %lu\n",
           (unsigned long)pthread_self());

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

    /* 1. Create socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* 2. Enable address reuse */
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

    /* 3. Bind */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1)
    {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* 4. Listen */
    if (listen(server_fd, BACKLOG) == -1)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening...\n");

    /* Accept Controllers continuously */
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

        /* Create a thread for this Controller */
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

        /* We don't need to join immediately */
        pthread_detach(thread_id);
    }

    close(server_fd);

    return 0;
}
