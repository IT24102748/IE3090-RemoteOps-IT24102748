#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410

#define BUFFER_SIZE 1024


int main(void)
{
    int sock_fd;

    struct sockaddr_in server_addr;

    char buffer[BUFFER_SIZE];


    printf("Controller starting...\n");
    printf("Connecting to %s:%d...\n",
           SERVER_IP,
           PORT);


    /* Create TCP socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }


    memset(&server_addr, 0, sizeof(server_addr));

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


    /* Connect to Agent */
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
     * Send AUTH command
     */
     const char *auth_command = "AUTH OPS-2748\n";

    printf("Sending: %s", auth_command);

    send(sock_fd,
         auth_command,
         strlen(auth_command),
         0);


    /*
     * Receive Agent response
     */
    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received = recv(sock_fd,
                                  buffer,
                                  sizeof(buffer) - 1,
                                  0);

    if (bytes_received > 0)
    {
        buffer[bytes_received] = '\0';
        printf("Agent response: %s", buffer);
    }


    printf("\nPress Enter to close connection...\n");

    getchar();


    close(sock_fd);

    return 0;
}
