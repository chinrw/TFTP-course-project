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
#include <netdb.h>
#include "helpers.h"

void child_process(struct tftp_request *request);

int send_ack(int sock, struct tftp_packet *packet, int size);

void handle_read(int socket, struct tftp_request *request);

void handle_write(int socket, struct tftp_request *request);

int send_data(int socket, struct tftp_packet *packet, int size);

int main(int argc, char **argv) {

    socklen_t addr_len = sizeof(struct sockaddr_in);;
    struct tftp_request *request;
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
    if (getsockname(udp_socket, reinterpret_cast<sockaddr *>(&server), &addr_len) != 0) {
        perror("server socket");
    }

    printf("server started at port [%d]\n", ntohs(server.sin_port));

    while (true) {
        request = (struct tftp_request *) calloc(1, sizeof(struct tftp_request));
        request->size = static_cast<int>(recvfrom(udp_socket, &(request->packet), MAX_REQUEST_SIZE, 0,
                                                  (struct sockaddr *) &(request->client), &addr_len));
        if (request->size < 0) {
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
            return EXIT_SUCCESS;
        } else {
            //parent
            continue;
        }
    }
    return EXIT_SUCCESS;
}

void child_process(struct tftp_request *request) {
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
    if (request->packet.cmd == RRQ) {
        printf("RRQ\n");
        handle_read(udp_socket, request);
    } else if (request->packet.cmd == WRQ) {
        printf("WRQ\n");
        handle_write(udp_socket, request);
    } else {
        perror("invalid request");
    }
    free(request);
    close(udp_socket);
    exit(EXIT_SUCCESS);
}

void handle_read(int socket, struct tftp_request *request) {
    struct tftp_packet send_packet;
    char fullpath[MAXFILENAMELENGTH] = {0};
    char *inputfile = request->packet.filename;// request file

    if (strlen(inputfile) + strlen(DEFAULT_DIRECTORY) >= MAXFILENAMELENGTH) {
        perror("request path too long");
        return;
    }

    // build fullpath
    strcpy(fullpath, DEFAULT_DIRECTORY);
    if (inputfile[0] != '/') {
        strcat(fullpath, "/");
    }
    strcat(fullpath, inputfile);

    printf("RRQ for file[%s]\n", fullpath);

    FILE *fp = fopen(fullpath, "r");
    if (fp == NULL) {
        fclose(fp);
        perror("File not exists!\n");
        return;
    }

    int send_size = 0;
    ushort counter = 1;
    send_packet.cmd = htons(DATA);
    do {
        send_packet.block = htons(counter);
        memset(send_packet.data, 0, sizeof(send_packet.data));
        send_size = fread(send_packet.data, 1, DATA_SIZE, fp);
        if (send_data(socket, &send_packet, send_size + 4) == -1) {
            fprintf(stderr, "failed to send packet number[%d]\n", counter);
            fclose(fp);
            return;
        }
        counter++;
    } while (send_size == DATA_SIZE);

    printf("file sent\n");
    fclose(fp);
}

int send_ack(int sock, struct tftp_packet *packet, int size) {
    if (send(sock, packet, static_cast<size_t>(size), 0) != size) {
        return -1;
    }
    return size;
}


void handle_write(int socket, struct tftp_request *request) {
    struct tftp_packet ack_packet, rcv_packet;
    char fullpath[MAXFILENAMELENGTH] = {0};
    char *inputfile = request->packet.filename;    // request file
    char *mode = inputfile + strlen(inputfile) + 1;
    char *blocksize_str = mode + strlen(mode) + 1;
    int blocksize = atoi(blocksize_str);

    if (blocksize <= 0 || blocksize > DATA_SIZE) {
        blocksize = DATA_SIZE;
    }

    if (strlen(inputfile) + strlen(DEFAULT_DIRECTORY) > sizeof(fullpath) - 1) {
        perror("request path too long");
        return;
    }

    // build fullpath
    memset(fullpath, 0, sizeof(fullpath));
    strcpy(fullpath, DEFAULT_DIRECTORY);
    if (inputfile[0] != '/') {
        strcat(fullpath, "/");
    }
    strcat(fullpath, inputfile);

    printf("WRQ: \"%s\", blocksize=%d\n", fullpath, blocksize);


    FILE *fp = fopen(fullpath, "r");
    if (fp != NULL) {
        // send error packet
        fclose(fp);
        printf("File \"%s\" already exists.\n", fullpath);
        return;
    }

    fp = fopen(fullpath, "w");
    if (fp == NULL) {
        printf("File \"%s\" create error.\n", fullpath);
        return;
    }
    int s_size = 0;
    int r_size = 0;
    ushort block = 1;
    int time_wait_data;

    ack_packet.cmd = htons(ACK);
    ack_packet.block = htons(0);

    if (send_ack(socket, &ack_packet, 4) == -1) {
        fprintf(stderr, "Error occurs when sending ACK = %d.\n", 0);
        goto wrq_error;
    }

    do {
        for (time_wait_data = 0; time_wait_data < PKT_RCV_TIMEOUT * PKT_MAX_RXMT; time_wait_data += 20000) {
            // Try receive(Nonblock receive).
            r_size = static_cast<int>(recv(socket, &rcv_packet, sizeof(struct tftp_packet), MSG_DONTWAIT));
            if (r_size > 0 && r_size < 4) {
                printf("Bad packet: r_size=%d, blocksize=%d\n", r_size, blocksize);
            }
            if (r_size >= 4 && rcv_packet.cmd == htons(DATA) && rcv_packet.block == htons(block)) {
                printf("DATA: block=%d, data_size=%d\n", ntohs(rcv_packet.block), r_size - 4);
                // Valid DATA
                fwrite(rcv_packet.data, 1, static_cast<size_t>(r_size - 4), fp);
                break;
            }
            usleep(20000);
        }
        if (time_wait_data >= PKT_RCV_TIMEOUT * PKT_MAX_RXMT) {
            printf("Receive timeout.\n");
            goto wrq_error;
        }

        ack_packet.block = htons(block);
        if (send_ack(socket, &ack_packet, 4) == -1) {
            fprintf(stderr, "Error occurs when sending ACK = %d.\n", block);
            goto wrq_error;
        }
        printf("Send ACK=%d\n", block);
        block++;
    } while (r_size == blocksize + 4);

    printf("Receive file end.\n");

    wrq_error:
    fclose(fp);
}


int send_data(int socket, struct tftp_packet *packet, int size) {
    struct tftp_packet rcv_packet;
    int retry_counter = 0;
    for (retry_counter = 0; retry_counter <= MAX_RETRY_RECV; retry_counter++) {
        printf("Send block=%d\n", ntohs(packet->block));
        if (send(socket, packet, size, 0) != size) {
            return -1;
        }
        usleep(10000);//0.1s for receier to respond
        int recv_size = (recv(socket, &rcv_packet, sizeof(struct tftp_packet),
                               MSG_DONTWAIT));//MSG_DONTWAIT->nonblock receive
        if (recv_size >= 4 && rcv_packet.cmd == htons(ACK) && rcv_packet.block == packet->block) {
            //printf("received ACK");
            break;
        } else {
            sleep(1);
        }
    }
    if (retry_counter >= MAX_RETRY_RECV) {
        // send timeout 10s
        fprintf(stderr, "Sent packet exceeded MAX_RETRY_RECV TIME[%ds]", PKT_MAX_RXMT);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
