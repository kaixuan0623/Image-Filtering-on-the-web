#define MAXLINE 1024
#define IMAGE_DIR "images/"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>  // Used to inspect directory contents.
#include "response.h"
#include "request.h"

// Functions for internal use only.
void write_image_list(int fd);
void write_image_response_header(int fd);


/*
 * Write the main.html response to the given fd.
 * This response dynamically populates the image-filter form with
 * the filenames located in IMAGE_DIR.
 */
void main_html_response(int fd) {
    char *header =
        "HTTP/1.1 200 OK\r\n"
        "Content-type: text/html\r\n\r\n";

    if(write(fd, header, strlen(header)) == -1) {
        perror("write");
    }

    FILE *in_fp = fopen("main.html", "r");
    char buf[MAXLINE];
    while (fgets(buf, MAXLINE, in_fp) > 0) {
        if(write(fd, buf, strlen(buf)) == -1) {
            perror("write");
        }
        // Insert a bit of dynamic Javascript into the HTML page.
        // This assumes there's only one "<script>" element in the page.
        if (strncmp(buf, "<script>", strlen("<script>")) == 0) {
            write_image_list(fd);
        }
    }
    fclose(in_fp);
}


/*
 * Write image directory contents to the given fd, in the format
 * "var filenames = ['<filename1>', '<filename2>', ...];\n"
 */
void write_image_list(int fd) {
    DIR *d = opendir(IMAGE_DIR);
    struct dirent *dir;

    dprintf(fd, "var filenames = [");
    if (d != NULL) {
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
                dprintf(fd, "'%s', ", dir->d_name);
            }
        }
        closedir(d);
    }
    dprintf(fd, "];\n");
}


/*
 * Given the socket fd and request data, do the following:
 * 1. Determine whether the request is valid according to the conditions
 *    under the "Input validation" section of Part 3 of the handout.
 *
 *    Ignore all other query parameters, and any other data in the request.
 *    Read about and use the "access" system call to check for the presence of
 *    files *with the correct permissions*.
 *
 * 2. If the request is invalid, send an informative error message as a response
 *    using the internal_server_error_response function.
 *
 * 3. Otherwise, write an appropriate HTTP header for a bitmap file
 *    ,and then use dup2 and execl to run
 *    the specified image filter and write the output directly to the socket.
 */
void image_filter_response(int fd, const ReqData *reqData) {

    // First valid the input.
    // reqData->method("GET"), reqData->path("/image-filter") has been checked.
    // Check if both query params "filter" and "image" are presented.
    // Only check the first two query params and ignore the others.
    int elem = 0;
    while (reqData->params[elem].name != NULL) {
        elem++;
    }

    if (elem < 2) {
        internal_server_error_response(fd, "Either query params 'filter' or 'image' is not presented.");
        exit(1);
    }

    int ret1 = strcmp(reqData->params[0].name, "image");
    int ret2 = strcmp(reqData->params[1].name, "filter");
    if ((ret1 != 0) || (ret2 != 0)) {
        internal_server_error_response(fd, "Either query params 'filter' or 'image' is not presented.");
        exit(1);
    }

    // Check if two query params contain '/'.
    if ((strchr(reqData->params[0].value, '/') != NULL) || (strchr(reqData->params[1].value, '/') != NULL)) {
        internal_server_error_response(fd, "Either value of 'filter' or 'image' contains '/'.");
        exit(1);
    }

    // Check if the filter value refer to an executable file under a4/filters/
    char *filepath = malloc(strlen("filters/") + strlen(reqData->params[1].value) + 1);
    strcpy(filepath, "filters/");
    strcat(filepath, reqData->params[1].value);

    int f1 = access(filepath, F_OK | X_OK);
    if (f1 != 0) {
        internal_server_error_response(fd, "the filter value doesn't refer to an executable file under a4/filters/.");
        exit(1);
    }

    // Check if the image value must refer to a readable file under a4/images/.
    char *imagepath = malloc(strlen("images/") + strlen(reqData->params[0].value) + 1);
    strcpy(imagepath, "images/");
    strcat(imagepath, reqData->params[0].value);

    int f2 = access(imagepath, F_OK | R_OK);
    if (f2 != 0) {
        internal_server_error_response(fd, "the image value doesn't refer to an readable file under a4/images/.");
        exit(1);
    }

    FILE *file = fopen(imagepath, "r");

    // write an appropriate HTTP header for a bitmap file.
    write_image_response_header(fd);

    // Reset stdin so when we read from stdin, it comes from the image file.
    // i.e. redirects the data from image file to stdin of the child process.
    if (dup2(fileno(file), STDIN_FILENO) == -1) {
        perror("dup2");
        exit(1);
    }

    // write the output directly to the socket.
    if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("dup2");
        exit(1);
    }

    // use execl to run the specified image filter.
    if (execl(filepath, filepath, NULL) == -1) {
        perror("execl");
        exit(1);
    }

    // close the image file.
    if (fclose(file) == -1) {
        perror("fclose");
        exit(1);
    }

    free(filepath);
    free(imagepath);
    // IMPLEMENT THIS
}


/*
 * Respond to an image-upload request.
 */
void image_upload_response(ClientState *client) {
    // First, extract the boundary string for the request. (i.e. "---7573")
    char *boundary = get_boundary(client);
    if (boundary == NULL) {
        bad_request_response(client->sock, "Couldn't find boundary string in request.");
        exit(1);
    }
    fprintf(stderr, "Boundary string: %s\n", boundary);

    // Use the boundary string to extract the name of the uploaded bitmap file.
    char *filename = get_bitmap_filename(client, boundary);
    if (filename == NULL) {
        bad_request_response(client->sock, "Couldn't find bitmap filename in request.");
        close(client->sock);
        exit(1);
    }

    // If the file already exists, send a Bad Request error to the user.
    // Or create a new image file in the directory: "images/"
    char *path = malloc(strlen(IMAGE_DIR) + strlen(filename) + 1);
    strcpy(path, IMAGE_DIR);
    strcat(path, filename);

    fprintf(stderr, "Bitmap path: %s\n", path);

    if (access(path, F_OK) >= 0) {
        bad_request_response(client->sock, "File already exists.");
        exit(1);
    }

    // Up to here, we have the name of the bitmap file uploaded stored in path.
    // The request has been modified to remove the first few lines.
    FILE *file = fopen(path, "wb");  // Create file with name: path for writing.

    // Read the bitmap image data from the request and write it
    // to the given fd (representing the file).
    int flag = save_file_upload(client, boundary, fileno(file));
    if (flag == -1) {
        bad_request_response(client->sock, "Bad Request.");
    }
    fclose(file);
    free(boundary);
    free(filename);
    free(path);
    see_other_response(client->sock, MAIN_HTML);
}


/*
 * Write the header for a bitmap image response to the given fd.
 */
void write_image_response_header(int fd) {
    char *response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/bmp\r\n"
        "Content-Disposition: attachment; filename=\"output.bmp\"\r\n\r\n";

    write(fd, response, strlen(response));
}


void not_found_response(int fd) {
    char *response =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain\r\n\r\n"
        "Page not found.\r\n";
    write(fd, response, strlen(response));
}


void internal_server_error_response(int fd, const char *message) {
    char *response =
        "HTTP/1.1 500 Internal Server Error\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
        "<html><head>\r\n"
        "<title>500 Internal Server Error</title>\r\n"
        "</head><body>\r\n"
        "<h1>Internal Server Error</h1>\r\n"
        "<p>%s<p>\r\n"
        "</body></html>\r\n";

    dprintf(fd, response, message);
}


void bad_request_response(int fd, const char *message) {
    char *response_header =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n\r\n";
    char *response_body =
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
        "<html><head>\r\n"
        "<title>400 Bad Request</title>\r\n"
        "</head><body>\r\n"
        "<h1>Bad Request</h1>\r\n"
        "<p>%s<p>\r\n"
        "</body></html>\r\n";
    char header_buf[MAXLINE];
    char body_buf[MAXLINE];
    sprintf(body_buf, response_body, message);
    sprintf(header_buf, response_header, strlen(body_buf));
    write(fd, header_buf, strlen(header_buf));
    write(fd, body_buf, strlen(body_buf));
    // Because we are making some simplfications with the HTTP protocol
    // the browser will get a "connection reset" message. This happens
    // because our server is closing the connection and terminating the process.
    // So this is really a hack.
    sleep(1);
}


void see_other_response(int fd, const char *other) {
    char *response =
        "HTTP/1.1 303 See Other\r\n"
        "Location: %s\r\n\r\n";

    dprintf(fd, response, other);
}
