#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410

#define BUFFER_SIZE 8192


int main(void)
{
    int sock_fd;

    struct sockaddr_in server_addr;

    char buffer[BUFFER_SIZE];

    char response[BUFFER_SIZE];


    printf("Controller starting...\n");

    printf("Connecting to %s:%d...\n",
           SERVER_IP,
           PORT);


    /*
     * Create TCP socket.
     */
    sock_fd = socket(AF_INET,
                     SOCK_STREAM,
                     0);


    if (sock_fd == -1)
    {
        perror("socket");

        exit(EXIT_FAILURE);
    }


    memset(&server_addr,
           0,
           sizeof(server_addr));


    server_addr.sin_family = AF_INET;

    server_addr.sin_port = htons(PORT);


    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");

        close(sock_fd);

        exit(EXIT_FAILURE);
    }


    /*
     * Connect to Agent.
     */
    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == -1)
    {
        perror("connect");

        close(sock_fd);

        exit(EXIT_FAILURE);
    }


    printf("Connected to Agent successfully.\n");


    /*
     * AUTHENTICATION
     */
    const char *auth_command = "AUTH OPS-2748\n";


    printf("Sending: %s", auth_command);


    send(sock_fd,
         auth_command,
         strlen(auth_command),
         0);


    memset(response,
           0,
           sizeof(response));


    ssize_t bytes_received;

    bytes_received = recv(sock_fd,
                          response,
                          sizeof(response) - 1,
                          0);


    if (bytes_received <= 0)
    {
        printf("Agent disconnected.\n");

        close(sock_fd);

        return 1;
    }


    response[bytes_received] = '\0';


    printf("Agent response: %s",
           response);


    /*
     * Interactive command loop.
     */
    while (1)
    {
        printf("\nEnter command: ");

        fflush(stdout);


        if (fgets(buffer,
                  sizeof(buffer),
                  stdin) == NULL)
        {
            break;
        }


        /*
         * Remove newline.
         */
        buffer[strcspn(buffer, "\r\n")] = '\0';


        /*
         * Exit command.
         */
        if (strcmp(buffer, "EXIT") == 0)
        {
            break;
        }


        /*
         * Send command.
         */
        char command_to_send[BUFFER_SIZE];


        snprintf(command_to_send,
                 sizeof(command_to_send),
                 "%s\n",
                 buffer);


        send(sock_fd,
             command_to_send,
             strlen(command_to_send),
             0);


        /*
         * Receive response.
         */
        memset(response,
               0,
               sizeof(response));


        bytes_received = recv(
            sock_fd,
            response,
            sizeof(response) - 1,
            0
        );


        if (bytes_received <= 0)
        {
            printf("Agent disconnected.\n");

            break;
        }


        response[bytes_received] = '\0';


        printf("Agent response:\n%s",
               response);
    }


    close(sock_fd);


    printf("Connection closed.\n");


    return 0;
}
