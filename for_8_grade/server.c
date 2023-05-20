#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define MAX_CLIENTS 2 // max number of gardeners
#define MAX_SPECTATORS 2 // max number of spectators
#define FLOWERS 40
#define BEING_WATERED 2

int flower_states[FLOWERS];
pthread_mutex_t flower_mutexes[FLOWERS];

int spectator_sockfds[MAX_SPECTATORS]; // Array of spectator sockets
int spectator_count = 0; // Count of spectators

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void initialize_flowers() {
    for (int i = 0; i < FLOWERS; i++) {
        flower_states[i] = 1; // all flowers are thirsty at the beginning
        pthread_mutex_init(&flower_mutexes[i], NULL);
    }
}

int allocate_flower() {
    for (int i = 0; i < FLOWERS; i++) {
        pthread_mutex_lock(&flower_mutexes[i]);
        if (flower_states[i] == 1) {
            flower_states[i] = BEING_WATERED; // mark flower as being watered
            pthread_mutex_unlock(&flower_mutexes[i]);
            return i;
        }
        pthread_mutex_unlock(&flower_mutexes[i]);
    }
    return -1; // no thirsty flowers
}

void *handle_gardener(void *arg) {
    int gardener_id = spectator_count++;
    int newsockfd = *(int *)arg;
    char buffer[256];
    int n;

    while (1) {
        int flower_index = allocate_flower();
        if (flower_index == -1) {
            sprintf(buffer, "-1");
            n = write(newsockfd, buffer, strlen(buffer)); // send -1 to client
            if (n < 0) error("ERROR writing to socket");
            break;
        }
sprintf(buffer, "%d", flower_index);
        n = write(newsockfd, buffer, strlen(buffer)); // send flower index to client
        if (n < 0) error("ERROR writing to socket");

        bzero(buffer, 256);
        n = read(newsockfd, buffer, 255); // wait for client to confirm watering
        if (n < 0) error("ERROR reading from socket");

        pthread_mutex_lock(&flower_mutexes[flower_index]);
        flower_states[flower_index] = 0; // mark flower as not thirsty
        pthread_mutex_unlock(&flower_mutexes[flower_index]);

        // send message to all spectators
        sprintf(buffer, "Gardener %d watered plant %d\n", gardener_id + 1, flower_index);
        for (int i = 0; i < spectator_count; i++) {
            write(spectator_sockfds[i], buffer, strlen(buffer));
        }
    }

    close(newsockfd);
    free(arg);

    return NULL;
}

int main(int argc, char *argv[]) {
    int sockfd, portno;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    if (argc < 2) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
        error("ERROR on binding");
    
    listen(sockfd, MAX_CLIENTS + MAX_SPECTATORS); // increase the queue size to allow for spectators
    clilen = sizeof(cli_addr);
    initialize_flowers();

    while (1) {
        int *newsockfd = malloc(sizeof(int));
        *newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (*newsockfd < 0) 
            error("ERROR on accept");

        char buffer[10];
        bzero(buffer, 10);
        read(*newsockfd, buffer, 9); // read the first message to identify gardener or spectator

        if (strcmp(buffer, "gardener") == 0) {
            pthread_t thread;
            pthread_create(&thread, NULL, handle_gardener, newsockfd);
        } else if (strcmp(buffer, "spectator") == 0 && spectator_count < MAX_SPECTATORS) {
            spectator_sockfds[spectator_count++] = *newsockfd;
            free(newsockfd);
        } else {
            free(newsockfd);
        }
    }

    close(sockfd);
    return 0; 
}
