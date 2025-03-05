#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "wfs.h"

#define COMM_PORT 5001


void open_cmd_sock()
{
    char buffer[BUFSIZ];
    struct sockaddr_in addr = {0}, client_address = {0};
    socklen_t client_len;
    ssize_t nbytes_read;
    int opt = 1;
    int newline_found = 0;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Could not create command socket\n");
        return;
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        fprintf(stderr, "Failed to set command socket options");
        goto err;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(COMM_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        fprintf(stderr, "Failed to bind command socket\n");
        goto err;
    }

    if (listen(sock, 3)) {
        fprintf(stderr, "Failed to listen on command socket\n");
        goto err;
    }

      while (1) {
        client_len = sizeof(client_address);
        int client_sock = accept(
            sock,
            (struct sockaddr*)&client_address,
            &client_len
        );

        while ((nbytes_read = read(client_sock, buffer, BUFSIZ)) > 0) {
            printf("received:\n");
            write(STDOUT_FILENO, buffer, nbytes_read);
            if (buffer[nbytes_read - 1] == '\n')
                newline_found = 1;
            for (int i = 0; i < nbytes_read - 1; i++)
                buffer[i]++;
            write(client_sock, buffer, nbytes_read);
           
        }
        close(client_sock);
    }
err:
    close(sock);
}