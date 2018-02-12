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
#define PORT 8000
#define MAX_REQUEST_SIZE 1024
// Max packet retransmission.
#define PKT_MAX_RXMT 3
#define PKT_SND_TIMEOUT 12*1000*1000
#define PKT_RCV_TIMEOUT 3*1000*1000
static char conf_document_root[MAXFILENAMELENGTH] = ".";

typedef struct {
	unsigned short int opcode;
	unsigned short int block;
	char data[BUF_LEN];
} TFTP_Data;

typedef struct {
	unsigned short int opcode;
	unsigned short int block;
} TFTP_Ack;

struct tftpx_packet{
	ushort cmd;
	union{
		ushort code;
		ushort block;
		// For a RRQ and WRQ TFTP packet
		char filename[2];
	};
	char data[DATA_SIZE];
};

struct tftpx_request{
	int size;
	struct sockaddr_in client;
	struct tftpx_packet packet;
};

/* Returns 0 for sucess, otherwise -1. */
int get_socket(struct sockaddr_in* s, char* host, int port) {
	s->sin_family = AF_INET;
	if (host != NULL) {
		struct hostent *he = gethostbyname(host);
		if (he == NULL) {
			perror("gethostbyname failed");
			return -1;
		}
		s->sin_addr = *((struct in_addr *)he->h_addr);
	} else {
		s->sin_addr.s_addr = htonl(INADDR_ANY);
	}
	s->sin_port = htons(port);
	memset(&(s->sin_zero), 0, 8);
	return 0;
}

int file_exists(char* filename){
    FILE* file = fopen(filename, "r");
    int ret;
    if (file){ret = 0;}
    else {ret = -1;}
    fclose(file);
    return ret;
}
