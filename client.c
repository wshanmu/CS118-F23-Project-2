#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "utils.h"


int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval tv;
    struct packet pkt;
    struct packet ack_pkt;
    char buffer[PAYLOAD_SIZE];
    unsigned short seq_num = 0;
    unsigned short ack_num = 0;
    char last = 0;
    char ack = 0;

    // read filename from command line argument
    if (argc != 2) {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Configure the server address structure to which we will send data
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT_TO);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the client address
    if (bind(listen_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Open file for reading
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }

    // TODO: Read from file, and initiate reliable data transfer to the server
    // no connection establishment
    // need to implement rdt: handshaking, seq, ack
    // congestion control (slow start, FR/FR: 3 dup ACKs/timeout/new ACK, Congestion Avoidance)
    // flow control (?)

    // first handshake
    // packet might be loss too. Need timer
    build_packet(&pkt, seq_num, ack_num, last, ack, PAYLOAD_SIZE, buffer);
    if(sendto(send_sockfd, (void*) &pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr_to, addr_size) < 0){
        printf("Cannot send message to server");
        return 1;
    }
    printSend(&pkt, 0);

    // receive ACK from server, expected ack=1
    int recv_len;
    recv_len = recvfrom(listen_sockfd, (void*) &pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr_from, &addr_size);
    if(recv_len < 0){
        printf("Cannot receive ACK from server");
        return 1;
    }
    printRecv(&pkt);

    // third handshake and send file size right now
    // obtain the file size
    fseek(fp, 0, SEEK_END);  // set file indicator to the end of the file
    long file_size = ftell(fp);  // get the current indicator (bytes)
    fseek(fp, 0, SEEK_SET);  // set back to the beginning
    // obtain file content
    char *file_content = malloc(file_size);
    fread(file_content, 1, file_size, fp);
    int segment_times = file_size / PAYLOAD_SIZE + 1;
    printf("times is: %d:\n", segment_times);

    seq_num += 1;
    ack_num = pkt.seqnum+1;
    sprintf(buffer, "%d", segment_times);
    build_packet(&pkt, seq_num, ack_num, last, ack, PAYLOAD_SIZE, buffer);
    if(sendto(send_sockfd, (void*) &pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr_to, addr_size) < 0){
        printf("Cannot send message to server");
        return 1;
    }
    printSend(&pkt, 0);


    // partition the file into small segments
    // and send segments to the server
    int packet_len = PAYLOAD_SIZE;
    for (int i = 0; i < segment_times; i++) {
        if (i == segment_times - 1) {
            packet_len = file_size - i * PAYLOAD_SIZE;
            last = 1;
        }
        strncpy(buffer, file_content + i * PAYLOAD_SIZE, packet_len);
        build_packet(&pkt, seq_num, ack_num, last, ack, packet_len, buffer);
        if(sendto(send_sockfd, (void*) &pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr_to, addr_size) < 0){
            printf("Cannot send file segment to server");
            return 1;
        }
        printSend(&pkt, 0);
        // begin a timer

        // wait for ACK
        recv_len = recvfrom(listen_sockfd, (void*) &pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr_from, &addr_size);
        if(recv_len < 0){
            printf("Cannot receive ACK from server");
            return 1;
        }
        printRecv(&pkt);
        if (pkt.acknum == seq_num + 1) {
            seq_num += 1;
            ack_num = pkt.seqnum + 1;
        }
    }

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

