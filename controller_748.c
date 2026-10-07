#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410

#define BUFFER_SIZE 8192

/* ---------------------------------------------------------
   Send all bytes
   --------------------------------------------------------- */
int send_all(int fd, const void *buffer, size_t length)
{
    size_t total = 0;
    const char *data = (const char *)buffer;

    while (total < length)
    {
        ssize_t sent = send(
            fd,
            data + total,
            length - total,
            0
        );

        if (sent <= 0)
        {
            return -1;
        }

        total += sent;
    }

    return 0;
}

/* ---------------------------------------------------------
   Receive and print response
   --------------------------------------------------------- */
int receive_response(int socket_fd)
{
    char buffer[BUFFER_SIZE];

    ssize_t received = recv(
        socket_fd,
        buffer,
        sizeof(buffer) - 1,
        0
    );

    if (received <= 0)
    {
        return -1;
    }

    buffer[received] = '\0';

    printf(
        "Agent response:\n%s",
        buffer
    );

    /*
       Add newline if the Agent response didn't have one.
    */
    if (received > 0 &&
        buffer[received - 1] != '\n')
    {
        printf("\n");
    }

    return 0;
}

/* ---------------------------------------------------------
   Remove newline from user input
   --------------------------------------------------------- */
void remove_newline(char *text)
{
    text[strcspn(text, "\r\n")] = '\0';
}

/* ---------------------------------------------------------
   PART L - Send file
   --------------------------------------------------------- */
int send_file(int socket_fd, const char *filename)
{
    FILE *file = fopen(filename, "rb");

    if (file == NULL)
    {
        perror("fopen");

        printf(
            "Could not open file: %s\n",
            filename
        );

        return -1;
    }

    /* Find file size */
    if (fseek(file, 0, SEEK_END) != 0)
    {
        perror("fseek");
        fclose(file);
        return -1;
    }

    long file_size = ftell(file);

    if (file_size < 0)
    {
        perror("ftell");
        fclose(file);
        return -1;
    }

    rewind(file);

    printf(
        "Uploading file: %s\n",
        filename
    );

    printf(
        "File size: %ld bytes\n",
        file_size
    );

    /*
       Send PUT header.
       Example:
       PUT test.txt 29
    */

    char header[BUFFER_SIZE];

    snprintf(
        header,
        sizeof(header),
        "PUT %s %ld\n",
        filename,
        file_size
    );

    printf(
        "Sending: %s",
        header
    );

    if (send_all(
            socket_fd,
            header,
            strlen(header)) < 0)
    {
        perror("send");
        fclose(file);
        return -1;
    }

    /*
       Immediately send exactly the file bytes.
    */

    char buffer[4096];

    size_t bytes_read;

    while ((bytes_read = fread(
                buffer,
                1,
                sizeof(buffer),
                file)) > 0)
    {
        if (send_all(
                socket_fd,
                buffer,
                bytes_read) < 0)
        {
            perror("send file");
            fclose(file);
            return -1;
        }
    }

    if (ferror(file))
    {
        perror("fread");
        fclose(file);
        return -1;
    }

    fclose(file);

    printf(
        "File bytes sent successfully.\n"
    );

    return receive_response(socket_fd);
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */
int main()
{
    int socket_fd;

    struct sockaddr_in server_address;

    printf("Controller starting...\n");

    /* Create socket */
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
        perror("inet_pton");
        close(socket_fd);
        return 1;
    }

    printf(
        "Connecting to %s:%d...\n",
        SERVER_IP,
        PORT
    );

    /* Connect */
    if (connect(
            socket_fd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)) < 0)
    {
        perror("connect");
        close(socket_fd);
        return 1;
    }

    printf(
        "Connected to Agent successfully.\n"
    );

    /* -----------------------------------------------------
       AUTH
       ----------------------------------------------------- */

    const char *auth_command =
        "AUTH OPS-2748\n";

    printf(
        "Sending: %s",
        auth_command
    );

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
        printf(
            "Failed to receive authentication response.\n"
        );

        close(socket_fd);
        return 1;
    }

    /* -----------------------------------------------------
       COMMAND LOOP
       ----------------------------------------------------- */

    char command[BUFFER_SIZE];

    while (1)
    {
        printf("\nEnter command: ");

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

        /* EXIT */
        if (strcmp(command, "EXIT") == 0)
        {
            char exit_command[] = "EXIT\n";

            send_all(
                socket_fd,
                exit_command,
                strlen(exit_command)
            );

            break;
        }

        /* -------------------------------------------------
           PUT command
           Example:
           PUT test.txt
           ------------------------------------------------- */

        if (strncmp(command, "PUT ", 4) == 0)
        {
            const char *filename =
                command + 4;

            if (strlen(filename) == 0)
            {
                printf(
                    "Usage: PUT <filename>\n"
                );

                continue;
            }

            send_file(
                socket_fd,
                filename
            );

            continue;
        }

        /* -------------------------------------------------
           Normal commands
           ------------------------------------------------- */

        char network_command[BUFFER_SIZE + 2];

        snprintf(
            network_command,
            sizeof(network_command),
            "%s\n",
            command
        );

        printf(
            "Sending: %s\n",
            command
        );

        if (send_all(
                socket_fd,
                network_command,
                strlen(network_command)) < 0)
        {
            perror("send");
            break;
        }

        if (receive_response(socket_fd) < 0)
        {
            printf(
                "Connection closed by Agent.\n"
            );

            break;
        }
    }

    close(socket_fd);

    printf(
        "Controller closed.\n"
    );

    return 0;
}
