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

#define BUF_LEN 512
#define RRQ 1
#define WRQ 2
#define DATA 3
#define ACK 4
#define ERROR 5
#define PORT 2369

void handle_read(struct sockaddr_in *sock_info, char *buffer, int buffer_len);

void handle_write(struct sockaddr_in *sock_info, char *buffer, int buffer_len);


int main() {
    ssize_t n;
    char buffer[BUF_LEN];
    socklen_t sockaddr_len;
    int server_socket;
    struct sigaction act;
    unsigned short int opcode;
    unsigned short int *opcode_ptr;
    struct sockaddr_in sock_info;

    /* Set up interrupt handlers */
    act.sa_handler = reinterpret_cast<void (*)(int)>(SIGCHLD);
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGCHLD, &act, NULL);

    act.sa_handler = reinterpret_cast<void (*)(int)>(SIGALRM);
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGALRM, &act, NULL);

    sockaddr_len = sizeof(sock_info);

    /* Set up UDP socket */
    memset(&sock_info, 0, sockaddr_len);

    sock_info.sin_addr.s_addr = htonl(INADDR_ANY);
    sock_info.sin_port = htons(PORT);
    sock_info.sin_family = PF_INET;

    server_socket = socket(PF_INET, SOCK_DGRAM, 0);

    if (server_socket < 0) {
        perror("socket");
        exit(-1);
    }

    int bind_result = bind(server_socket, (struct sockaddr *) &sock_info, sockaddr_len);
    if (bind_result < 0) {
        perror("bind");
        exit(-1);
    }

    /* Get port and print it */
    getsockname(server_socket, (struct sockaddr *) &sock_info, &sockaddr_len);

    printf("%d\n", ntohs(sock_info.sin_port));

    /* Receive the first packet and deal w/ it accordingly */
    while (true) {
        intr_recv:
        n = recvfrom(server_socket, buffer, BUF_LEN, 0,
                     (struct sockaddr *) &sock_info, &sockaddr_len);
        if (n < 0) {
            if (errno == EINTR) goto intr_recv;
            perror("recvfrom");
            exit(-1);
        }
        /* check the opcode */
        opcode_ptr = (unsigned short int *) buffer;
        opcode = ntohs(*opcode_ptr);
        if (opcode != RRQ && opcode != WRQ) {
            /* Illegal TFTP Operation */
            *opcode_ptr = htons(ERROR);
            *(opcode_ptr + 1) = htons(4);
            *(buffer + 4) = 0;
            intr_send:
            n = sendto(server_socket, buffer, 5, 0,
                       (struct sockaddr *) &sock_info, sockaddr_len);
            if (n < 0) {
                if (errno == EINTR) goto intr_send;
                perror("sendto");
                exit(-1);
            }
        } else {
            if (fork() == 0) {
                /* Child - handle the request */
                close(server_socket);
                break;
            } else {
                /* Parent - continue to wait */
            }
        }
    }

    if (opcode == RRQ) handle_read(&sock_info, buffer, BUF_LEN);
    if (opcode == WRQ) handle_write(&sock_info, buffer, BUF_LEN);

    return 0;
}

void handle_read(struct sockaddr_in *sock_info, char *buffer, int buffer_len) {

}

void handle_write(struct sockaddr_in *sock_info, char *buffer, int buffer_len) {

}