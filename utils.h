#ifndef UTILS_H
#define UTILS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MACROS
#define SERVER_IP "127.0.0.1"
#define LOCAL_HOST "127.0.0.1"
#define SERVER_PORT_TO 5002
#define CLIENT_PORT 6001
#define SERVER_PORT 6002
#define CLIENT_PORT_TO 5001
#define PAYLOAD_SIZE 1099
#define WINDOW_SIZE 5
#define TIMEOUT 0
#define TIMEOUT_MS 500000
#define MAX_SEQUENCE 1024
#define print_flag 0



// Packet Layout
// You may change this if you want to
struct packet {
    unsigned short seqnum;
    unsigned short acknum;
    char ack;
    char last;
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};

// Utility function to build a packet
void build_packet(struct packet* pkt, unsigned short seqnum, unsigned short acknum, char last, char ack, unsigned int length, const char* payload) {
    pkt->seqnum = seqnum;
    pkt->acknum = acknum;
    pkt->ack = ack;
    pkt->last = last;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}

// Utility function to print a packet
void printRecv(struct packet* pkt, int side) {
    if (side == 0) { // client，I only care about the ACK
        printf("RECV ack:%d%s%s\n", pkt->acknum, pkt->last ? " LAST": "", (pkt->ack) ? " ACK": "");
    } else if (side == 1) { // server, I only care about the seq
        printf("RECV seq:%d%s%s\n", pkt->seqnum, pkt->last ? " LAST": "", (pkt->ack) ? " ACK": "");
    } else {
        printf("RECV seq:%d ack:%d%s%s\n", pkt->seqnum, pkt->acknum, pkt->last ? " LAST": "", (pkt->ack) ? " ACK": "");
    }
}

void printSend(struct packet* pkt, int resend, int side) {
    if (side == 0) {
        if (resend)
            printf("RESEND seq:%d %s%s\n", pkt->seqnum, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
        else
            printf("SEND seq:%d %s%s\n", pkt->seqnum, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
    } else if (side == 1) {
        if (resend)
            printf("RESEND ack:%d%s%s\n", pkt->acknum, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
        else
            printf("SEND ack:%d%s%s\n", pkt->acknum, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
    } else {
        if (resend)
            printf("RESEND seq:%d ack:%d%s%s\n", pkt->seqnum, pkt->acknum, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
        else
            printf("SEND seq:%d ack:%d%s%s\n", pkt->seqnum, pkt->acknum, pkt->last ? " LAST": "", pkt->ack ? " ACK": "");
    }

}
#endif