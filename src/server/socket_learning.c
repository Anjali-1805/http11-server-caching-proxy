#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(void)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
         sizeof(server_addr)) < 0) {
    perror("bind failed");
    close(server_fd);
    return 1;
}

if (listen(server_fd, 5) < 0) {
    perror("listen failed");
    close(server_fd);
    return 1;
}

printf("Server is listening on port 8080.\n");
int client_fd = accept(server_fd, NULL, NULL);

if (client_fd < 0) {
    perror("accept failed");
    close(server_fd);
    return 1;
}

printf("A client connected successfully.\n");

close(client_fd);
close(server_fd);

return 0;
}
