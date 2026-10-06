#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410

int main(void)
{
    int sock_fd;
    struct sockaddr_in server_addr;

    printf("Controller starting...\n");
    printf("Connecting to %s:%d...\n", SERVER_IP, PORT);

    /* 1. Create TCP socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Clear server address structure */
    memset(&server_addr, 0, sizeof(server_addr));

    /* IPv4 */
    server_addr.sin_family = AF_INET;

    /* Port 9410 */
    server_addr.sin_port = htons(PORT);

    /* Convert IP address */
    if (inet_pton(AF_INET, SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    /* 2. Connect to Agent */
    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) == -1)
    {
        perror("connect");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to Agent successfully.\n");

    /* Keep connection open for testing */
    printf("TCP connection established.\n");

    getchar();

    close(sock_fd);

    printf("Connection closed.\n");

    return 0;
}
