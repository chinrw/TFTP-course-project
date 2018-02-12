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
#include <iostream>
#include "helpers.h"

//using namespace std;

void child_process(struct tftp_request *request);

int send_ack(int sock, struct tftp_packet *packet, int size);

void handle_read(int sock, struct tftp_request *request);

void handle_write(int sock, struct tftp_request *request);

int send_packet(int sock, struct tftp_packet *packet, int size);


int main(int argc, char **argv) {
    int sock;
    socklen_t addr_len;
    pthread_t t_id;
    struct sockaddr_in server;
    unsigned short port = PORT;

    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        printf("Server socket could not be created.\n");
        return 0;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *) &server, sizeof(server)) < 0) {
        printf("Server bind failed. Server already running? Proper permissions?\n");
        return 2;
    }

    printf("Server started at 0.0.0.0:%d.\n", port);

    struct tftp_request *request;
    addr_len = sizeof(struct sockaddr_in);
    while (1) {
        request = (struct tftp_request *) malloc(sizeof(struct tftp_request));
        memset(request, 0, sizeof(struct tftp_request));
        request->size = recvfrom(
                sock, &(request->packet), MAX_REQUEST_SIZE, 0,
                (struct sockaddr *) &(request->client),
                &addr_len);
        request->packet.cmd = ntohs(request->packet.cmd);
        printf("Receive request.\n");
        int pid = fork();
        if (pid < 0) {
            perror("fork\n");
        } else if (pid == 0) {
            printf("child\n");
            child_process(request);
            break;
        } else {
            printf("parent\n");
        }
    }

    return 0;
}

void child_process(struct tftp_request *request) {
    printf("sth in child process\n");
    int sock;
    struct sockaddr_in server;
    static socklen_t addr_len = sizeof(struct sockaddr_in);

    // if(request->size <= 0){
    // 	printf("Bad request.\n");
    // 	return NULL;
    // }//TODO

    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("child socket");
        exit(EXIT_FAILURE);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = 0;

    if (bind(sock, (struct sockaddr *) &server, sizeof(server)) < 0) {
        perror("child bind");
        exit(EXIT_FAILURE);
    }

    if (connect(sock, (struct sockaddr *) &(request->client), addr_len) < 0) {
        perror("child connect");
        exit(EXIT_FAILURE);
    }

    // Choose handler
    switch (request->packet.cmd) {
        case RRQ:
            printf("RRQ called.\n");
            handle_read(sock, request);
            break;
        case WRQ:
            printf("WRQ called.\n");
            handle_write(sock, request);
            break;
        default:
            perror("Illegal operation");
            break;
    }
    free(request);
    close(sock);
    exit(EXIT_SUCCESS);
}

void handle_read(int sock, struct tftp_request *request) {
    struct tftp_packet snd_packet;
    char fullpath[MAXFILENAMELENGTH];
    char *r_path = request->packet.filename;    // request file
    char *mode = r_path + strlen(r_path) + 1;
    char *blocksize_str = mode + strlen(mode) + 1;
    int blocksize = atoi(blocksize_str);

    if (blocksize <= 0 || blocksize > DATA_SIZE) {
        blocksize = DATA_SIZE;
    }

    if (strlen(r_path) + strlen(conf_document_root) > sizeof(fullpath) - 1) {
        perror("Request path too long");
        return;
    }

    // build fullpath
    memset(fullpath, 0, sizeof(fullpath));
    strcpy(fullpath, conf_document_root);
    if (r_path[0] != '/') {
        strcat(fullpath, "/");
    }
    strcat(fullpath, r_path);
    printf("rrq: \"%s\", blocksize=%d\n", fullpath, blocksize);

    //if(!strncasecmp(mode, "octet", 5) && !strncasecmp(mode, "netascii", 8)){
    //	// send error packet
    //	return;
    //}

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
        if (send_packet(sock, &snd_packet, s_size + 4) == -1) {
            fprintf(stderr, "Error occurs when sending packet.block = %d.\n", block);
            goto rrq_error;
        }
        block++;
    } while (s_size == blocksize);

    printf("\nSend file end.\n");

    rrq_error:
    fclose(fp);
}

int send_ack(int sock, struct tftp_packet *packet, int size) {
    if (send(sock, packet, size, 0) != size) {
        return -1;
    }

    return size;
}


void handle_write(int sock, struct tftp_request *request) {
    struct tftp_packet ack_packet, rcv_packet;
    char fullpath[256];
    char *r_path = request->packet.filename;    // request file
    char *mode = r_path + strlen(r_path) + 1;
    char *blocksize_str = mode + strlen(mode) + 1;
    int blocksize = atoi(blocksize_str);

    if (blocksize <= 0 || blocksize > DATA_SIZE) {
        blocksize = DATA_SIZE;
    }

    if (strlen(r_path) + strlen(conf_document_root) > sizeof(fullpath) - 1) {
        printf("Request path too long. %d\n", static_cast<int>(strlen(r_path) + strlen(conf_document_root)));
        return;
    }

    // build fullpath
    memset(fullpath, 0, sizeof(fullpath));
    strcpy(fullpath, conf_document_root);
    if (r_path[0] != '/') {
        strcat(fullpath, "/");
    }
    strcat(fullpath, r_path);

    printf("wrq: \"%s\", blocksize=%d\n", fullpath, blocksize);

    //if(!strncasecmp(mode, "octet", 5) && !strncasecmp(mode, "netascii", 8)){
    //	// send error packet
    //	return;
    //}

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
    if (send_ack(sock, &ack_packet, 4) == -1) {
        fprintf(stderr, "Error occurs when sending ACK = %d.\n", 0);
        goto wrq_error;
    }

    do {
        for (time_wait_data = 0; time_wait_data < PKT_RCV_TIMEOUT * PKT_MAX_RXMT; time_wait_data += 20000) {
            // Try receive(Nonblock receive).
            r_size = static_cast<int>(recv(sock, &rcv_packet, sizeof(struct tftp_packet), MSG_DONTWAIT));
            if (r_size > 0 && r_size < 4) {
                printf("Bad packet: r_size=%d, blocksize=%d\n", r_size, blocksize);
            }
            if (r_size >= 4 && rcv_packet.cmd == htons(DATA) && rcv_packet.block == htons(block)) {
                printf("DATA: block=%d, data_size=%d\n", ntohs(rcv_packet.block), r_size - 4);
                // Valid DATA
                fwrite(rcv_packet.data, 1, r_size - 4, fp);
                break;
            }
            usleep(20000);
        }
        if (time_wait_data >= PKT_RCV_TIMEOUT * PKT_MAX_RXMT) {
            printf("Receive timeout.\n");
            goto wrq_error;
            break;
        }

        ack_packet.block = htons(block);
        if (send_ack(sock, &ack_packet, 4) == -1) {
            fprintf(stderr, "Error occurs when sending ACK = %d.\n", block);
            goto wrq_error;
        }
        printf("Send ACK=%d\n", block);
        block++;
    } while (r_size == blocksize + 4);

    printf("Receive file end.\n");

    wrq_error:
    fclose(fp);

    return;

}

int send_packet(int sock, struct tftp_packet *packet, int size) {
    struct tftp_packet rcv_packet;
    int time_wait_ack = 0;
    int rxmt = 0;
    int r_size = 0;

    for (rxmt = 0; rxmt < PKT_MAX_RXMT; rxmt++) {
        printf("Send block=%d\n", ntohs(packet->block));
        if (send(sock, packet, size, 0) != size) {
            return -1;
        }
        for (time_wait_ack = 0; time_wait_ack < PKT_RCV_TIMEOUT; time_wait_ack += 10000) {
            // Try receive(Nonblock receive).
            r_size = static_cast<int>(recv(sock, &rcv_packet, sizeof(struct tftp_packet), MSG_DONTWAIT));
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
