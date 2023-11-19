#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

#include <time.h>

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    struct packet buffer;
    socklen_t addr_size = sizeof(client_addr_from);
    int expected_seq_num = 0, seq_num_server = 100;
    int recv_len;
    struct packet ack_pkt;
    char payload[PAYLOAD_SIZE];
    char last = 0;

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the server address
    if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Configure the client address structure to which we will send ACKs
    memset(&client_addr_to, 0, sizeof(client_addr_to));
    client_addr_to.sin_family = AF_INET;
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    client_addr_to.sin_port = htons(CLIENT_PORT_TO);

    // Open the target file for writing (always write to output.txt)
    FILE *fp = fopen("output.txt", "wb");


    time_t start, end;
    time(&start);

    // TODO: Receive file from the client and save it as output.txt
    while (1) {
//        // receive first handshake
//        recv_len = recvfrom(listen_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr_from, &addr_size);
//        if (recv_len < 0) {
//            printf("Cannot receive from client\n");
//            return 1;
//        } else {
//            printf("Receive initial packet from client\n");
//            printRecv(&buffer);
//        }
//        // return first ACK
//        expected_seq_num = buffer.seqnum + 1;
//        build_packet(&ack_pkt, seq_num_server, expected_seq_num, 0, 1, PAYLOAD_SIZE, payload);
//        if(sendto(listen_sockfd, (void*) &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&client_addr_to, addr_size) < 0){
//            printf("Cannot send message to server");
//            return 1;
//        }
//        printSend(&ack_pkt, 0);
//
//        // receive third handshake, including the number of segments
//        recv_len = recvfrom(listen_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr_from, &addr_size);
//        if (recv_len < 0) {
//            printf("Cannot receive from client\n");
//            return 1;
//        } else {
//            printRecv(&buffer);
//            printf("File segment number is %s\n", buffer.payload);
//        }
//
//        // return second ACK
//        expected_seq_num = buffer.seqnum + 1;
//        seq_num_server += 1;
//        build_packet(&ack_pkt, seq_num_server, expected_seq_num, 0, 1, PAYLOAD_SIZE, payload);
//        if(sendto(listen_sockfd, (void*) &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&client_addr_to, addr_size) < 0){
//            printf("Cannot send message to server");
//            return 1;
//        }
//        printSend(&ack_pkt, 0);

        // receive file segments
        while (1) {
            if (recvfrom(listen_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *) &client_addr_from,
                         &addr_size) < 0) {
                printf("Cannot receive from client\n");
                return 1;
            }
            printRecv(&buffer);
//            printf("current expected seq num: %d\n", expected_seq_num);

            if (buffer.seqnum == expected_seq_num) {
                expected_seq_num += 1;
                seq_num_server += 1;
                fwrite(buffer.payload, sizeof(char),buffer.length, fp);
                printf("Write %d-th segment into the output file.\n", buffer.seqnum + 1);
                if (buffer.last == 1) {
                    break;
                }
            }
            build_packet(&ack_pkt, seq_num_server, expected_seq_num, buffer.last, 1, PAYLOAD_SIZE, payload);
            if(sendto(listen_sockfd, (void*) &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&client_addr_to, addr_size) < 0){
                printf("Cannot send message to server");
                return 1;
            }
            printSend(&ack_pkt, 0);
        }
        break;
    }

    time(&end);
    double time_taken = end - start;
    printf ("It took %.2f seconds.\n", time_taken);

    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
