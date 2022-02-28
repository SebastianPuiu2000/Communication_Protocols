#include <stdio.h>      /* printf, sprintf */
#include <stdlib.h>     /* exit, atoi, malloc, free */
#include <unistd.h>     /* read, write, close */
#include <string.h>     /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h>      /* struct hostent, gethostbyname */
#include <arpa/inet.h>
#include "helpers.h"
#include "requests.h"
#include "parson.h"

#define MAX_LENGTH 100

int main(int argc, char *argv[])
{
    char *message = NULL;
    char *response = NULL;
    int sockfd;

    int logged = 0;
    int exit = 0;
    int invalid_command = 1;
    int invalid_number;

    char *command = NULL;
    char *username = NULL, *password = NULL;
    char *cookie = NULL;
    char* token = NULL;
    char *title = NULL, *author = NULL, *genre = NULL;
    char *publisher = NULL, *id = NULL, *page_count = NULL;


    while(exit == 0) {

        invalid_command = 1;

        command = calloc(MAX_LENGTH, sizeof(char));
        fgets(command, MAX_LENGTH, stdin);
        strtok(command, "\n");

        if(strcmp(command, "register") == 0) {

            invalid_command = 0;

            username = calloc(MAX_LENGTH, sizeof(char));
            password = calloc(MAX_LENGTH, sizeof(char));

            printf("username=");
            fgets(username, MAX_LENGTH, stdin);
            strtok(username, "\n");

            printf("password=");
            fgets(password, MAX_LENGTH, stdin);
            strtok(password, "\n");


            sockfd = open_connection("34.118.48.238", 8080, AF_INET, SOCK_STREAM, 0);


            JSON_Value *root_value = json_value_init_object();
            JSON_Object *root_object = json_value_get_object(root_value);
            char *serialized_string = NULL;
            json_object_set_string(root_object, "username", username);
            json_object_set_string(root_object, "password", password);
            
            serialized_string = json_serialize_to_string_pretty(root_value);
            
            message = compute_post_request("34.118.48.238", "/api/v1/tema/auth/register", "application/json",serialized_string, NULL, NULL);

            send_to_server(sockfd, message);

            response = receive_from_server(sockfd);


            close_connection(sockfd);


            if(strstr(response, "400 Bad Request") != NULL) {
                printf("Error: This username is taken!\n");
            }
            else {
                printf("Succes: Register completed!\n");
            }

            json_free_serialized_string(serialized_string);
            json_value_free(root_value);

            free(username);
            free(password);

        }

        if(strcmp(command, "login") == 0) {

            invalid_command = 0;

            if(logged == 1) {
                printf("Already logged in!\n"); 
                continue;
            }

            username = calloc(MAX_LENGTH, sizeof(char));
            password = calloc(MAX_LENGTH, sizeof(char));
            
            printf("username=");
            fgets(username, MAX_LENGTH, stdin);
            strtok(username, "\n");

            printf("password=");
            fgets(password, MAX_LENGTH, stdin);
            strtok(password, "\n");


            sockfd = open_connection("34.118.48.238", 8080, AF_INET, SOCK_STREAM, 0);


            JSON_Value *root_value = json_value_init_object();
            JSON_Object *root_object = json_value_get_object(root_value);
            char *serialized_string = NULL;
            json_object_set_string(root_object, "username", username);
            json_object_set_string(root_object, "password", password);
            
            serialized_string = json_serialize_to_string_pretty(root_value);

            message = compute_post_request("34.118.48.238", "/api/v1/tema/auth/login", "application/json",serialized_string, NULL, NULL);

            send_to_server(sockfd, message);

            response = receive_from_server(sockfd);


            close_connection(sockfd);


            json_free_serialized_string(serialized_string);
            json_value_free(root_value);

            free(username);
            free(password);

            

            if(strstr(response, "400 Bad Request") != NULL) {
                printf("Error: Credentials are not good!\n");
            }
            else {
                printf("Succes: Login confirmed!\n");

                cookie = strstr(response, "connect.sid");
                strtok(cookie, ";");
                printf("Cookie: %s\n", cookie);

                logged = 1;
            }
        }

        if(strcmp(command, "enter_library") == 0) {

            invalid_command = 0;

            if(logged == 0) {
                printf("Error: Not logged in!\n");
                continue;
            }


            sockfd = open_connection("34.118.48.238", 8080, AF_INET, SOCK_STREAM, 0);


            message = compute_get_request("34.118.48.238", "/api/v1/tema/library/access", NULL, cookie, NULL);

            send_to_server(sockfd, message);

            response = receive_from_server(sockfd);


            close_connection(sockfd);


            if(strstr(response, "200 OK")) {
                printf("Succes: Token confirmed!\n");

                token = strstr(response, "token");
                token = &token[8];
                strtok(token, "\"");
                printf("Token: %s\n", token);
            }
            else {
                printf("Error: Token denied!\n");
            }  
        }

        if(strcmp(command, "get_books") == 0) {

            invalid_command = 0;

            if(token == NULL) {
                printf("Error: No access to library!\n");
                continue;
            }

            if(logged == 0) {
                printf("Error: Not logged in!\n");
                continue;
            }


            sockfd = open_connection("34.118.48.238", 8080, AF_INET, SOCK_STREAM, 0);


            message = compute_get_request("34.118.48.238", "/api/v1/tema/library/books", NULL, NULL, token);

            send_to_server(sockfd, message);

            response = receive_from_server(sockfd);


            close_connection(sockfd);


            if(strstr(response, "200 OK")) {
                printf("Succes: Books extracted!\n");
                response = strstr(response, "[");

                if(!strstr(response, "{")) {
                    printf("Warning: Library empty!\n");
                }

                printf("%s\n", response);
            }
            else {
                printf("Error: Books not extracted!\n");
            }
        }

        if(strcmp(command, "get_book") == 0) {

            invalid_command = 0;

            if(token == NULL) {
                printf("Error: No access to library!\n");
                continue;
            }

            if(logged == 0) {
                printf("Error: Not logged in!\n");
                continue;
            }

            id = calloc(MAX_LENGTH, sizeof(char));

            printf("id=");
            fgets(id, MAX_LENGTH, stdin);
            strtok(id, "\n");

            if(strstr(id, "-")) {
                printf("Error: Negative number!\n");
                free(id);
                continue;
            }

            invalid_number = 0;
            for(int i = 0; i < strlen(id); i++) {

                if(id[i] < 48 || id[i] > 57) {
                    invalid_number = 1;
                }
            }

            if(invalid_number == 1) {
                printf("Error: Invalid number!\n");
                free(id);
                continue;
            }

            char books_path[100] = "/api/v1/tema/library/books/";

            strcat(books_path, id);


            sockfd = open_connection("34.118.48.238", 8080, AF_INET, SOCK_STREAM, 0);


            message = compute_get_request("34.118.48.238", books_path, NULL, NULL, token);

            send_to_server(sockfd, message);

            response = receive_from_server(sockfd);


            close_connection(sockfd);


            if(strstr(response, "200 OK")) {
                printf("Succes: Book found!\n");
                response = strstr(response, "[");
                printf("%s\n", response);
            }
            else {
                printf("Error: Book not found!\n");
            }

            free(id);
        }

        if(strcmp(command, "add_book") == 0) {

            invalid_command = 0;

            if(token == NULL) {
                printf("Error: No access to library!\n");
                continue;
            }

            if(logged == 0) {
                printf("Error: Not logged in!\n");
                continue;
            }

            title = calloc(MAX_LENGTH, sizeof(char));
            author = calloc(MAX_LENGTH, sizeof(char));
            genre = calloc(MAX_LENGTH, sizeof(char));
            page_count = calloc(MAX_LENGTH, sizeof(char));
            publisher = calloc(MAX_LENGTH, sizeof(char));

            printf("title=");
            fgets(title, MAX_LENGTH, stdin);
            strtok(title, "\n");

            printf("author=");
            fgets(author, MAX_LENGTH, stdin);
            strtok(author, "\n");

            printf("genre=");
            fgets(genre, MAX_LENGTH, stdin);
            strtok(genre, "\n");

            printf("publisher=");
            fgets(publisher, MAX_LENGTH, stdin);
            strtok(publisher, "\n");

            printf("page_count=");
            fgets(page_count, MAX_LENGTH, stdin);
            strtok(page_count, "\n");

            if(strstr(page_count, "-")) {
                printf("Error: Negative number!\n");
                free(title);
                free(author);
                free(genre);
                free(publisher);
                free(page_count);
                continue;
            }

            invalid_number = 0;
            for(int i = 0; i < strlen(page_count); i++) {

                if(page_count[i] < 48 || page_count[i] > 57) {
                    invalid_number = 1;
                }
            }

            if(invalid_number == 1) {
                printf("Error: Invalid number!\n");
                free(title);
                free(author);
                free(genre);
                free(publisher);
                free(page_count);
                continue;
            }


            sockfd = open_connection("34.118.48.238", 8080, AF_INET, SOCK_STREAM, 0);


            JSON_Value *root_value = json_value_init_object();
            JSON_Object *root_object = json_value_get_object(root_value);
            char *serialized_string = NULL;
            json_object_set_string(root_object, "title", title);
            json_object_set_string(root_object, "author", author);
            json_object_set_string(root_object, "genre", genre);
            json_object_set_number(root_object, "page_count", atoi(page_count));
            json_object_set_string(root_object, "publisher", publisher);

            serialized_string = json_serialize_to_string_pretty(root_value);

            message = compute_post_request("34.118.48.238", "/api/v1/tema/library/books", "application/json", serialized_string, NULL, token);

            send_to_server(sockfd, message);

            response = receive_from_server(sockfd);


            close_connection(sockfd);


            if(strstr(response, "200 OK")) {
                printf("Succes: Book added!\n");
            }
            else {
                printf("Error: Book not added!\n");
            }

            json_free_serialized_string(serialized_string);
            json_value_free(root_value);

            free(title);
            free(author);
            free(genre);
            free(publisher);
        }

        if(strcmp(command, "delete_book") == 0) {

            invalid_command = 0;

            if(token == NULL) {
                printf("Error: No access to library!\n");
                continue;
            }

            if(logged == 0) {
                printf("Error: Not logged in!\n");
                continue;
            }
            
            id = calloc(MAX_LENGTH, sizeof(char));

            printf("id=");
            fgets(id, MAX_LENGTH, stdin);
            strtok(id, "\n");

            if(strstr(id, "-")) {
                printf("Error: Negative number!\n");
                free(id);
                continue;
            }

            invalid_number = 0;
            for(int i = 0; i < strlen(id); i++) {

                if(id[i] < 48 || id[i] > 57) {
                    invalid_number = 1;
                }
            }

            if(invalid_number == 1) {
                printf("Error: Invalid number!\n");
                free(id);
                continue;
            }

            char books_path[100] = "/api/v1/tema/library/books/";

            strcat(books_path, id);


            sockfd = open_connection("34.118.48.238", 8080, AF_INET, SOCK_STREAM, 0);


            message = compute_delete_request("34.118.48.238", books_path, NULL, NULL, token);

            send_to_server(sockfd, message);

            response = receive_from_server(sockfd);


            close_connection(sockfd);


            if(strstr(response, "200 OK")) {
                printf("Succes: Book deleted!\n");
            }
            else {
                printf("Error: Book not deleted!\n");
            }

            free(id);
        }

        if(strcmp(command, "logout") == 0) {

            invalid_command = 0;

            if(logged == 0) {
                printf("Error: Not logged in!\n");
                continue;
            }


            sockfd = open_connection("34.118.48.238", 8080, AF_INET, SOCK_STREAM, 0);


            message = compute_get_request("34.118.48.238", "/api/v1/tema/auth/logout", NULL, cookie, NULL);

            send_to_server(sockfd, message);

            response = receive_from_server(sockfd);


            close_connection(sockfd);


            if(strstr(response, "200 OK")) {
                printf("Succes: Logout accepted!\n");
                cookie = NULL;
                token = NULL;
                logged = 0;
            }
            else {
                printf("Error: Logout denied!\n");
            }   
        }

        if(strcmp(command, "exit") == 0) {

            invalid_command = 0;
            exit = 1;
            printf("Exit program...\n");
        }

        if(invalid_command == 1) {
            printf("Error: Invalid command!\n");
            printf("Try one of these commands:\n");
            printf("- register\n");
            printf("- login\n");
            printf("- enter_library\n");
            printf("- get_book\n");
            printf("- delete_book\n");
            printf("- get_books\n");
            printf("- logout\n");
            printf("- exit\n");
        }

        free(command);
    }

    return 0;
}
