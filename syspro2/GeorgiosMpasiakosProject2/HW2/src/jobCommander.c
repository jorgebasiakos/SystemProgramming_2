#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

void perror_exit(char *message);

void client(int sock, int argc, char **argv);

int main(int argc, char *argv[])
{
    int port, sock, i;
    struct sockaddr_in server;
    struct addrinfo hints, *res, *rp;

    if (argc < 3 || !((strcmp(argv[3], "exit") == 0) || (strcmp(argv[3], "setConcurrency") == 0) || (strcmp(argv[3], "issueJob") == 0) || (strcmp(argv[3], "stop") == 0) || (strcmp(argv[3], "poll") == 0)))
    {
        printf("Commander: Error command. Exiting..\n");
        exit(1);
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       //=> IPv4
    hints.ai_socktype = SOCK_STREAM; //=> TCP

    if (getaddrinfo(argv[1], argv[2], &hints, &res) != 0)
    { // POSIX STANDARD
        perror_exit("getaddrinfo");
    }

    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1)
        {
            continue;
        }

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) != -1)
        {
            break;
        }

        close(sock);
    }

    if (rp == NULL)
    {
        perror_exit("connect");
    }

    freeaddrinfo(res);

    printf("Connecting to %s port %s\n", argv[1], argv[2]);

    client(sock, argc, argv);

    close(sock);
    return 0;
}

void perror_exit(char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

void client(int sock, int argc, char **argv)
{
    int totalLength = 0;
    for (int i = 3; i < argc; ++i)
    { // Calculate the total length of the arguments
        totalLength += strlen(argv[i]);
    }

    char *argsString = (char *)malloc(totalLength + argc + 1); // Allocate memory to store the concatenated arguments

    int currentPosition = 0;
    for (int i = 3; i < argc; ++i)
    {                                                  // Concatenate argv strings (ektos apo to prwto)
        strcpy(argsString + currentPosition, argv[i]); // Copy the current argument from argv to argsString
        currentPosition += strlen(argv[i]);            // Update the currentPosition to point to the next available position in argsString
        strcpy(argsString + currentPosition, " ");     // Add a space after the current argument in argsString
        currentPosition += 1;                          // Move the currentPosition to the position after the added space
    }

    printf("Commander: Args string: %s\n", argsString);

    if (write(sock, argsString, strlen(argsString) + 1) == -1)
    { // Apostoli Job ston Executor
        perror("Commander: Error writing to writefd");
        exit(EXIT_FAILURE);
    }

    char output_buffer[1024];
    memset(output_buffer, 0x0, 1024); // Clear the output buffer
    int bytes_read;
    while ((bytes_read = read(sock, output_buffer, 1024)) > 0)
    {                                                                     // Read the response from the executor and store it in the output buffer
        printf("Commander: Response from executor: %s\n", output_buffer); // print output
        if (strcmp(output_buffer, "end") == 0)
        {
            return;
        }
    }

    free(argsString); //++
}
