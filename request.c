#include "request.h"
#include "response.h"
#include <string.h>


/******************************************************************************
 * ClientState-processing functions
 *****************************************************************************/
ClientState *init_clients(int n) {
    ClientState *clients = malloc(sizeof(ClientState) * n);
    for (int i = 0; i < n; i++) {
        clients[i].sock = -1;  // -1 here indicates available entry
    }
    return clients;
}

/*
 * Remove the client from the client array, free any memory allocated for
 * fields of the ClientState struct, and close the socket.
 */
void remove_client(ClientState *cs) {
    if (cs->reqData != NULL) {
        free(cs->reqData->method);
        free(cs->reqData->path);
        for (int i = 0; i < MAX_QUERY_PARAMS && cs->reqData->params[i].name != NULL; i++) {
            free(cs->reqData->params[i].name);
            free(cs->reqData->params[i].value);
        }
        free(cs->reqData);
        cs->reqData = NULL;
    }
    close(cs->sock);
    cs->sock = -1;
    cs->num_bytes = 0;
}


/*
 * Search the first <inbuf> characters of buf for a network newline ("\r\n").
 * Return the index *immediately after* the location of the '\n' of the 1st "\r\n"
 * if the network newline is found, or -1 otherwise.
 * Definitely do not use strchr or any other string function in here. (Why not?)
 */
int find_network_newline(const char *buf, int inbuf) {
    int i = 0;
    while (i < inbuf) {
        if ((buf[i] == '\r') && (buf[i + 1] == '\n')) {
            return i + 2;
        }
        i++;
    }
    return -1;
}

/*
 * Removes one line (terminated by \r\n) from the client's buffer.
 * Update client->num_bytes accordingly.
 *
 * For example, if `client->buf` contains the string "hello\r\ngoodbye\r\nblah",
 * after calling remove_line on it, buf should contain "goodbye\r\nblah"
 * Remember that the client buffer is *not* null-terminated automatically.
 */
void remove_buffered_line(ClientState *client) {
    int position = find_network_newline(client->buf, client->num_bytes);
    if (position == -1) {
        fprintf(stderr, "network newline is not found\n");
    } else {
        // Removes one line (terminated by \r\n) from the client's buffer.
        // Move the stuff after the network newline '\r\n' to the beginning
        // of client->buf using memmove.
        memmove(client->buf, client->buf + position, client->num_bytes - position);
        client->buf[client->num_bytes - position] = '\0'; // null-terminated it.

        // Update client->num_bytes.
        client->num_bytes -= position;
    }
    // IMPLEMENT THIS
}


/*
 * Read some data into the client buffer. Append new data to data already
 * in the buffer.  Update client->num_bytes accordingly.
 * Return the number of bytes read in, or -1 if the read failed.
 */
int read_from_client(ClientState *client) {
    int numRead = 0;

    // if the client->buf is full, overwrite it.
    if (client->num_bytes == MAXLINE - 1) {

        numRead = read(client->sock, client->buf, MAXLINE - 1);

        if (numRead < 0) { // error
            perror("read");
            return -1;
        }

        client->num_bytes = numRead;
        client->buf[client->num_bytes] = '\0';
        return numRead;

    } else {
        // Space left in client->buf is MAXLINE - client->num_bytes, -1 is for '\0'.
        numRead = read(client->sock, client->buf + client->num_bytes, MAXLINE - client->num_bytes - 1);

        if (numRead < 0) { // error
            perror("read");
            return -1;
        }

        client->num_bytes = client->num_bytes + numRead;
        client->buf[client->num_bytes] = '\0';
        return numRead;
    }
    // IMPLEMENT THIS
    // replace the return line with something more appropriate
    // return -1;
}


/*****************************************************************************
 * Parsing the start line of an HTTP request.
 ****************************************************************************/
// Helper function declarations.
void parse_query(ReqData *req, const char *str);
void update_fdata(Fdata *f, const char *str);
void fdata_free(Fdata *f);
void log_request(const ReqData *req);


/* If there is a full line (terminated by a network newline (CRLF))
 * then use this line to initialize client->reqData
 * Return 0 if a full line has not been read, 1 otherwise.
 */
int parse_req_start_line(ClientState *client) {

    int end = find_network_newline(client->buf, client->num_bytes);
    if (end == -1) {
        fprintf(stderr, "network newline is not found\n");
        return 0;  // a full line has not been read.
    }

    // allocate space for client->reqData on heap.
    client->reqData = malloc(sizeof(ReqData));

    int path_len = 0; // the length of the path.

    // initialize <method> first using malloc().
    if ((client->buf[0] == 'G') && (client->buf[1] == 'E') && (client->buf[2] == 'T')) {

        client->reqData->method = malloc(strlen(GET) + 1);
        memcpy(client->reqData->method, GET, 4);

        // next we initialize <path> using malloc().
        while ((client->buf[path_len + 4] != '?') && (client->buf[path_len + 4] != ' ')) {
            path_len++;
        }
        client->reqData->path = malloc(path_len + 1);  // add 1 for '\0'

        int i = 4;
        while ((client->buf[i] != '?') && (client->buf[i] != ' ')) {
            // memcpy(client->reqData->path + i - 4, client->buf[i], 1);
            client->reqData->path[i - 4] = client->buf[i];
            i++;
        }
        // client->reqData->path + path_len = '\0';
        client->reqData->path[path_len] = '\0';

        // next we initialize client->reqData->params.
        if (client->buf[4 + path_len] == '?') {  // means there are query parameters.

            int query_len = 0;
            while (client->buf[query_len + path_len + 5] != ' ') {
                query_len++;
            }
            char query_str[query_len];

            int ptr = path_len + 5;
            while (client->buf[ptr] != ' ') {
                query_str[ptr - (path_len + 5)] = client->buf[ptr];
                ptr++;
            }

            query_str[ptr - (path_len + 5)] = '\0';

            // Initializes client->reqData->params from the key-value pairs contained in query_str.
            parse_query(client->reqData, query_str);


        } else {  // means there are no query parameter.
            for (int j = 0; j < MAX_QUERY_PARAMS; j++) {
                // set all the names and values to NULL.
                client->reqData->params[j].name = NULL;
                client->reqData->params[j].value = NULL;
            }
        }

    } else if ((client->buf[0] == 'P') && (client->buf[1] == 'O') && (client->buf[2] == 'S') && (client->buf[3] == 'T')) {

        client->reqData->method = malloc(strlen(POST) + 1);
        memcpy(client->reqData->method, POST, 5);

        // next we initialize <path> using malloc().
        while (client->buf[path_len + 5] != ' ') {
            path_len++;
        }
        client->reqData->path = malloc(path_len + 1);  // add 1 for '\0'

        int i = 5;
        while (client->buf[i] != ' ') {
            // memcpy(client->reqData->path + i - 5, client->buf[i], 1);
            client->reqData->path[i - 5] = client->buf[i];
            i++;
        }
        // client->reqData->path + path_len = '\0';
        client->reqData->path[path_len] = '\0';

        // since for POST method, there is no query parameters, set them to NULL.
        for (int j = 0; j < MAX_QUERY_PARAMS; j++) {
            // set all the names and values to NULL.
            client->reqData->params[j].name = NULL;
            client->reqData->params[j].value = NULL;
        }
    }


    // IMPLEMENT THIS

    // This part is just for debugging purposes.
    log_request(client->reqData);
    return 1;
}


/*
 * Initializes req->params from the key-value pairs contained in the given
 * string <str>.
 * Assumes that the string is the part after the '?' in the HTTP request target,
 * e.g., name1=value1&name2=value2.
 */
void parse_query(ReqData *req, const char *str) {
    // Check how many key-value pairs contained in str by checking number of "&".
    int numParam = 1;
    for (int p = 0; p < strlen(str); p++) {
        if (str[p] == '&') {
            numParam++;
        }
    }

    if (numParam > MAX_QUERY_PARAMS) {  //error checking.
        fprintf(stderr, "Invaild request: the maximum number of query params is 5. Program exits.\n");
        exit(1);
    }
    // req->params[i] = malloc(sizeof(Fdata));  // allocate enough space for each req->params.

    char *token = strtok((char *) str, "&");

    int count = 0;
    while(token != NULL) {
        // first find the index of '='.
        int eq = 0;
        for (int q = 0; q < strlen(token); q++) {
            if (token[q] == '=') {
                eq = q;
                break;
            }
        }
        // error checking, if '=' not found or is token[0], means invalid input query params.
        if (eq == 0) {
            fprintf(stderr, "Invaild request: the format of query params is 'name=value'. Program exits.\n");
            exit(1);
        }

        // add 1*sizeof(char) to make room for '\0'.
        req->params[count].name = malloc((eq + 1) * sizeof(char));
        req->params[count].value = malloc((strlen(token) - eq) * sizeof(char));

        for (int q = 0; q < eq; q++) {
            req->params[count].name[q] = token[q];
        }
        req->params[count].name[eq] = '\0';

        for (int q = eq + 1; q < strlen(token); q++) {
            req->params[count].value[q - (eq + 1)] = token[q];
        }
        req->params[count].value[strlen(token) - (eq + 1)] = '\0';

        // go to the next name-value pairs.
        token = strtok(NULL, "&");
        count++;
    }
}


/*
 * Print information stored in the given request data to stderr.
 */
void log_request(const ReqData *req) {
    fprintf(stderr, "Request parsed: [%s] [%s]\n", req->method, req->path);
    for (int i = 0; i < MAX_QUERY_PARAMS && req->params[i].name != NULL; i++) {
        fprintf(stderr, "  %s -> %s\n",
                req->params[i].name, req->params[i].value);
    }
}


/******************************************************************************
 * Parsing multipart form data (image-upload)
 *****************************************************************************/

char *get_boundary(ClientState *client) {
    int len_header = strlen(POST_BOUNDARY_HEADER);

    while (1) {
        int where = find_network_newline(client->buf, client->num_bytes);
        if (where > 0) {
            if (where < len_header || strncmp(POST_BOUNDARY_HEADER, client->buf, len_header) != 0) {
                remove_buffered_line(client);
            } else {
                // We've found the boundary string!
                // the line contains the boundary string does not end with
                // '\r\n', instead, it ends with the boundary string.
                // We are going to add "--" to the beginning to make it easier
                // to match the boundary line later
                char *boundary = malloc(where - len_header + 1);
                strncpy(boundary, "--", where - len_header + 1);
                strncat(boundary, client->buf + len_header, where - len_header - 1);
                boundary[where - len_header] = '\0';
                return boundary;
            }
        } else {
            // Need to read more bytes
            if (read_from_client(client) <= 0) {
                // Couldn't read; this is a bad request, so give up.
                return NULL;
            }
        }
    }
    return NULL;
}


char *get_bitmap_filename(ClientState *client, const char *boundary) {
    int len_boundary = strlen(boundary);

    // Read until finding the boundary string.
    while (1) {
        int where = find_network_newline(client->buf, client->num_bytes);
        if (where > 0) {
            if (where < len_boundary + 2 ||
                    strncmp(boundary, client->buf, len_boundary) != 0) {
                remove_buffered_line(client);
            } else {
                // We've found the line with the boundary!
                remove_buffered_line(client);
                break;
            }
        } else {
            // Need to read more bytes
            if (read_from_client(client) <= 0) {
                // Couldn't read; this is a bad request, so give up.
                return NULL;
            }
        }
    }

    int where = find_network_newline(client->buf, client->num_bytes);

    client->buf[where - 1] = '\0';  // Used for strrchr to work on just the single line.
    char *raw_filename = strrchr(client->buf, '=') + 2;
    int len_filename = client->buf + where - 3 - raw_filename;
    char *filename = malloc(len_filename + 1);
    strncpy(filename, raw_filename, len_filename);
    filename[len_filename] = '\0';

    // Restore client->buf
    client->buf[where - 1] = '\n';
    remove_buffered_line(client);
    return filename;
}

/*
 * Read the file data from the socket and write it to the file descriptor
 * file_fd.
 * You know when you have reached the end of the file in one of two ways:
 *    - search for the boundary string in each chunk of data read
 * ( the "\r\n" that comes before the boundary string, and the
 * "--\r\n" that comes after.)
 *    - extract the file size from the bitmap data, and use that to determine
 * how many bytes to read from the socket and write to the file
 */
int save_file_upload(ClientState *client, const char *boundary, int file_fd) {
    // Read in the next two lines: Content-Type line, and empty line
    remove_buffered_line(client);
    remove_buffered_line(client);

    int total_bytes = 0; // total number of bytes we read.
    int numRead = read_from_client(client); // read from socket, store it to client->buf.
    int file_size;
    memcpy(&file_size, client->buf + 2, 4);   // get the size of the bitmap file.
    printf("file_size: %d\n", file_size);

    write(file_fd, client->buf, numRead);
    total_bytes += numRead;

    while ((numRead = read_from_client(client)) > 0) {
        total_bytes += numRead;
        if (total_bytes >= file_size) {  // we should stop reading.
            write(file_fd, client->buf, numRead - (total_bytes - file_size));
            return 0;
        }

        write(file_fd, client->buf, numRead);

    }

    return 0;
}
