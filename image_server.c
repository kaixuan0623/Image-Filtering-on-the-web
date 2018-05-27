#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>    /* Internet domain header */

#include "socket.h"
#include "request.h"
#include "response.h"

#ifndef PORT
#define PORT 30000
#endif

#define BACKLOG 10
#define MAX_CLIENTS 10


/*
 * Read data from a client socket, and, if there is enough information to
 * determine the type of request, spawn a child process to respond to the
 * request.
 *
 * Return 1 if one of the conditions hold:
 *   a) No bytes were read from the socket. (The client has likely closed the
 *      connection.)
 *   b) A child process has been created to respond to the request.
 *
 * This return value indicates that the server process should close the socket.
 * Otherwise, return 0 (indicating that the server must continue to monitor the
 * socket).
 */
int handle_client(ClientState *client) {
    // Read in data (request) from the client's socket into its buffer,
    // (just first few line of the request)
    // and update num_bytes. If no bytes were read, return 1.

    // number of bytes read from client's socket.
    int numRead = read(client->sock, client->buf, sizeof(client->buf) - 1);
    // sizeof(client->buf) - 1 = MAXLINE - 1,
    // since sizeof(client->buf) is MAXLINE, we have to give a space for buf
    // to include a '\0'.

    if (numRead == 0) {
        //  No bytes were read from the socket. (The client has likely closed the connection.)
        return 1;

    } else if (numRead > 0) {
        client->num_bytes = numRead;  // The number of bytes currently in the buffer.
        client->buf[numRead] = '\0';  // null-terminate it explicitly.

        // Next update ReqData using parse_req_start_line(client).
        parse_req_start_line(client);

        // Now client->reqData has been initialized.

    } else if (numRead < 0) {  // error checking.
        perror("read");
        return -1;
    }

    // At this point client->reqData is not null, and so we are guaranteed
    // to spawn a child process to handle the request (so we return 1).
    // First, call fork. In the *parent* process, just return 1.
    // In the *child* process, check the values in client->reqData to determine
    // how to respond to the request.
    // The child should call exit(0) (rather than return) to prevent it from
    // executing the main server loop that listens for new requests.

    int result = fork();
    if (result < 0) {  // error checking.
        perror("fork");
        exit(1);
    } else if (result > 0) {  // parent process.
        return 1;

    } else if (result == 0) {  // child process.
        // when typing the URL in browser, the 1st request is sent,
        // to render the provided main.html page.
        int ret1 = strcmp(client->reqData->method, GET);
        int ret2 = strcmp(client->reqData->path, MAIN_HTML);
        int ret3 = strcmp(client->reqData->path, IMAGE_FILTER);
        int ret4 = strcmp(client->reqData->method, POST);
        int ret5 = strcmp(client->reqData->path, IMAGE_UPLOAD);

        if ((ret1 == 0) && (ret2 == 0)) {
            // Render the provided main.html page.
            main_html_response(client->sock);
        } else if ((ret1 == 0) && (ret3 == 0)) {
            // Presses the "Run filter" bottom, another request will be sent.
            image_filter_response(client->sock, client->reqData);
        } else if ((ret4 == 0) && (ret5 == 0)) {
            image_upload_response(client);
        } else {
            // Render the "Not Found" string.
            not_found_response(client->sock);
        }

        // call image_filter_response(int fd, const ReqData *reqData) here.
        // check if the resource name is "/image-filter".
        // int ret3 = strcmp(client->reqData->path, IMAGE_FILTER);
        // if ((ret1 == 0) && (ret3 == 0)) {
        //     image_filter_response(client->sock, client->reqData);
        // }
    }
    //IMPLEMENT THIS

    close(client->sock);
    exit(0);
}


int main(int argc, char **argv) {
    // Creates an array of ClientState of size MAX_CLIENTS = 10.
    ClientState *clients = init_clients(MAX_CLIENTS);

    struct sockaddr_in *servaddr = init_server_addr(PORT);

    // Create an fd to listen to new connections.
    int listenfd = setup_server_socket(servaddr, BACKLOG);

    // Print out information about this server
    char host[MAX_HOSTNAME];
    if ((gethostname(host, sizeof(host))) == -1) {
        perror("gethostname");
        exit(1);
    }
    fprintf(stderr, "Server hostname: %s\n", host);
    fprintf(stderr, "Port: %d\n", PORT);

    // Set up the arguments for select
    int maxfd = listenfd;
    fd_set allset;

    // initialize allset and add listenfd to the
    // set of file descriptors passed into select.
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);

    // Set up a timer for select (This is only necessary for debugging help)
    struct timeval timer;


    // Main server loop.
    while (1) {
        // make a copy of the set before we pass it into select.
        fd_set rset = allset;
        timer.tv_sec = 2;
        timer.tv_usec = 0;

        int nready = select(maxfd + 1, &rset, NULL, NULL, &timer);
        // nready is numbers of ready file_descriptors if not 0 and -1.
        // if nready is 0, means timer expired.
        if(nready == -1) {
            perror("select");
            exit(1);
        }

        if(nready == 0) {  // timer expired
            // Check if any children have failed
            int status;
            int pid;
            errno = 0;
            if ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                if (WIFSIGNALED(status)) {
                    fprintf(stderr, "Child [%d] failed with signal %d\n", pid,
                            WTERMSIG(status));
                }
            }
            continue;
        }


        if (FD_ISSET(listenfd, &rset)) {    // New client connection.
            int new_client_fd = accept_connection(listenfd);  // block until one client connects.
            if (new_client_fd >= 0) {
                maxfd = (new_client_fd > maxfd) ? new_client_fd : maxfd;
                FD_SET(new_client_fd, &allset);    // Add new descriptor to set.

                for(int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].sock < 0) {
                        clients[i].sock = new_client_fd;
                        break;
                    }
                }
            }

            nready -= 1;
        }

        // The nready is just an optimization; no harm in checking all fds
        // except efficiency
        for (int i = 0; i < MAX_CLIENTS && nready > 0; i++) {
            // Check whether clients[i] has an active, ready socket.
            if (clients[i].sock < 0 || !FD_ISSET(clients[i].sock, &rset)) {
                continue;
            }

            int done = handle_client(&clients[i]);
            if (done) {
                FD_CLR(clients[i].sock, &allset);  // remove the socket fd associate with this client.
                remove_client(&clients[i]);
            }

            nready -= 1;
        }
    }
}
