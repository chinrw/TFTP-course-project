#include <cstdio>
#include <sys/socket.h>
#include <sys/types.h>
#include <csignal>
#include <netinet/ip.h>
#include <cstring>
#include <arpa/inet.h>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include "helpers.h"

void child_process(struct tftpx_request *request);

void handle_read(int socket, struct tftpx_request *request);

void handle_write(int socket, struct tftpx_request *request);

int send_packet(int socket, struct tftpx_packet *packet, int size);

int main(int argc, char **argv) {

    socklen_t addr_len = sizeof(struct sockaddr_in);;
    pthread_t t_id;
    struct tftpx_request* request;
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    int udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket < 0) {
        perror("server socket");
        exit(EXIT_FAILURE);
    }

    if (bind(udp_socket, (struct sockaddr *) &server, sizeof(server)) < 0) {
        perror("server bind");
        exit(EXIT_FAILURE);
    }

    printf("server started at port [%d]\n", PORT);

    while (1) {
        request = (struct tftpx_request *) calloc(1,sizeof(struct tftpx_request));
        request->size = recvfrom(udp_socket, &(request->packet), MAX_REQUEST_SIZE, 0,
                        (struct sockaddr *) &(request->client),&addr_len);
        if(request->size < 0){
            perror("recvfrom");
            continue;
         }
        request->packet.cmd = ntohs(request->packet.cmd);
        printf("receive message\n");
        //fork
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
        } else if (pid == 0) {
            //child
            child_process(request);
            break;
        } else {
            //parent
            continue;
        }
    }
    return 0;
}

void child_process(struct tftpx_request *request) {
    printf("child process\n");
    struct sockaddr_in server;
    static socklen_t addr_len = sizeof(struct sockaddr_in);

    int udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket < 0) {
        perror("child socket");
        exit(EXIT_FAILURE);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = 0;

    if (bind(udp_socket, (struct sockaddr *) &server, sizeof(server)) < 0) {
        perror("child bind");
        exit(EXIT_FAILURE);
    }

    if (connect(udp_socket, (struct sockaddr *) &(request->client), addr_len) < 0) {
        perror("child connect");
        exit(EXIT_FAILURE);
    }

    // Choose handler
    if(request->packet.cmd == RRQ){
        printf("RRQ\n");
        handle_read(udp_socket, request);
    }
    else if(request->packet.cmd == WRQ){
        printf("WRQ\n");
        handle_write(udp_socket, request);
    }
    else{
        perror("invalid request");
    }
    free(request);
    close(udp_socket);
    exit(EXIT_SUCCESS);
}

void handle_read(int socket, struct tftpx_request *request) {
    struct tftpx_packet snd_packet;
    char fullpath[MAXFILENAMELENGTH] = {0};
    char* r_path = request->packet.filename;// request file
    char* mode = r_path + strlen(r_path) + 1;
    char* blocksize_str = mode + strlen(mode) + 1;
    int blocksize = atoi(blocksize_str);

    if (blocksize <= 0 || blocksize > DATA_SIZE) {
        blocksize = DATA_SIZE;
    }

    if (strlen(r_path) + strlen(default_directory) >= MAXFILENAMELENGTH) {
        perror("request path too long");
        return;
    }

    // build fullpath
    strcpy(fullpath, default_directory);
    if (r_path[0] != '/') {
        strcat(fullpath, "/");
    }
    strcat(fullpath, r_path);

    printf("RRQ: \"%s\", blocksize=%d\n", fullpath, blocksize);

    FILE *fp = fopen(fullpath, "r");
    if (fp == NULL) {
        perror("File not exists!\n");
        return;
    }

    int s_size = 0;
    ushort block = 1;
    snd_packet.cmd = htons(DATA);
    do {
        memset(snd_packet.data, 0, sizeof(snd_packet.data));
        snd_packet.block = htons(block);
        s_size = static_cast<int>(fread(snd_packet.data, 1, static_cast<size_t>(blocksize), fp));
        if (send_packet(socket, &snd_packet, s_size + 4) == -1) {
            fprintf(stderr, "Error occurs when sending packet.block = %d.\n", block);
            fclose(fp);
            return
        }
        block++;
    } while (s_size == blocksize);

    printf("file sent\n");
    fclose(fp);
}

void handle_write(int socket, struct tftpx_request *request) {

}

int send_packet(int socket, struct tftpx_packet *packet, int size) {
    struct tftpx_packet rcv_packet;
    int time_wait_ack = 0;
    int rxmt = 0;
    int r_size = 0;

    for (rxmt = 0; rxmt < PKT_MAX_RXMT; rxmt++) {
        printf("Send block=%d\n", ntohs(packet->block));
        if (send(socket, packet, size, 0) != size) {
            return -1;
        }
        for (time_wait_ack = 0; time_wait_ack < PKT_RCV_TIMEOUT; time_wait_ack += 10000) {
            // Try receive(Nonblock receive).
            r_size = static_cast<int>(recv(socket, &rcv_packet, sizeof(struct tftpx_packet), MSG_DONTWAIT));
            if (r_size >= 4 && rcv_packet.cmd == htons(ACK) && rcv_packet.block == packet->block) {
                //printf("ACK: block=%d\n", ntohs(rcv_packet.block));
                // Valid ACK
                break;
            }
            usleep(10000);
        }
        if (time_wait_ack < PKT_RCV_TIMEOUT) {
            break;
        } else {
            // Retransmission.
            continue;
        }
    }
    if (rxmt == PKT_MAX_RXMT) {
        // send timeout
        printf("Sent packet exceeded PKT_MAX_RXMT.\n");
        return -1;
    }

    return size;
}
