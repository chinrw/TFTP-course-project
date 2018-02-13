#pragma once
#include <netdb.h>
#include <string>
#define BUF_LEN 512
#define MAXFILENAMELENGTH 256
#define DATA_SIZE 512

#define RRQ 1
#define WRQ 2
#define DATA 3
#define ACK 4
#define ERROR 5

#define PORT 0
#define MAX_REQUEST_SIZE 1024
// Max packet retransmission.
#define PKT_MAX_RXMT 10
#define MAX_RETRY_SEND 10
#define MAX_RETRY_RECV 10
#define PKT_RCV_TIMEOUT 3*1000*1000
static char DEFAULT_DIRECTORY[MAXFILENAMELENGTH] = ".";

typedef struct {
    unsigned short int opcode;
    unsigned short int block;
    char data[BUF_LEN];
} TFTP_Data;

typedef struct {
    unsigned short int opcode;
    unsigned short int block;
} TFTP_Ack;

struct tftp_packet {
    ushort cmd;
    union {
        ushort code;
        ushort block;
        // For a RRQ and WRQ TFTP packet
        char filename[2];
    };
    char data[DATA_SIZE];
};

struct tftp_request {
    int size;
    struct sockaddr_in client;
    struct tftp_packet packet;
};
