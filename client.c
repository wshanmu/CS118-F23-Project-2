#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/errno.h>

#include "utils.h"

//volatile sig_atomic_t timeout_occurred = 0;
//
//void handler(int sig) {
//    printf("Timeout occur: waiting for ACK\n");
//    timeout_occurred = 1;
//}

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
    int cwnd = 1;
    int ssthreshold = 20;

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

    // set timeout interval for recvfrom function
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;
    if (setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("set socket failed");
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
//    freopen("client_output_message.txt", "w", stdout); // redirect stdout to a file


    // TODO: Read from file, and initiate reliable data transfer to the server
    // no connection establishment
    // need to implement rdt: handshaking, seq, ack
    // congestion control (slow start, FR/FR: 3 dup ACKs/timeout/new ACK, Congestion Avoidance)

    // obtain the file size
    fseek(fp, 0, SEEK_END);  // set file indicator to the end of the file
    long file_size = ftell(fp);  // get the current indicator (bytes)
    fseek(fp, 0, SEEK_SET);  // set back to the beginning
    // obtain file content
    char *file_content = malloc(file_size);
    fread(file_content, 1, file_size, fp);
    int packet_num = file_size / PAYLOAD_SIZE;
    if (file_size % PAYLOAD_SIZE != 0) {
        packet_num += 1;
    }
    printf("Packet number is: %d.\n", packet_num);

    int packet_len = PAYLOAD_SIZE;
    int largest_seq_num = seq_num + packet_num - 1;
    int wnd_left = seq_num;
    int wnd_right = wnd_left + cwnd;
    int j = 0; // for fast retransmission
    int biggest_send = 0; // biggest seq num in sent packets
    int resend_flag = 0;
    while (1) {
        // send all packets in the congestion window
        if (!resend_flag) {
            printf("Begin Sending Packet in Window: [%d, %d]\n", wnd_left, wnd_right-1);
            for (; seq_num < wnd_right; seq_num++) {
                if (seq_num == largest_seq_num) {
                    packet_len = file_size - (seq_num) * PAYLOAD_SIZE;
                    last = 1;
                    printf("Going to send the last packet.\n");
                } else {
                    last = 0;
                }
                strncpy(buffer, file_content + (seq_num) * PAYLOAD_SIZE, packet_len);
                build_packet(&pkt, seq_num, ack_num, last, ack, packet_len, buffer);
                if(sendto(send_sockfd, (void*) &pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr_to, addr_size) < 0){
                    printf("Cannot send file segment to server");
                    return 1;
                }
                printSend(&pkt, 0, 0);
                usleep(100);
            }
            biggest_send = wnd_right - 1;
        } else {
            printf("Begin Resending the Lost Packet %d, Current Window: [%d, %d]\n", wnd_left, wnd_left, wnd_right-1);
            if (seq_num == largest_seq_num) {
                packet_len = file_size - (seq_num) * PAYLOAD_SIZE;
                last = 1;
                printf("Going to send the last packet.\n");
            } else {
                last = 0;
            }
            strncpy(buffer, file_content + (seq_num) * PAYLOAD_SIZE, packet_len);
            build_packet(&pkt, seq_num, ack_num, last, ack, packet_len, buffer);
            if(sendto(send_sockfd, (void*) &pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr_to, addr_size) < 0){
                printf("Cannot send file segment to server");
                return 1;
            }
            printSend(&pkt, 1, 0);
            resend_flag = 0;
            seq_num = wnd_right > biggest_send ? biggest_send : wnd_right;
        }


        // wait for ACK, ack_num should be seq_num++
        while (1) {
            if(recvfrom(listen_sockfd, (void*) &pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr_from, &addr_size) < 0){
                if (errno == EAGAIN) {  // fail to receive ACK within TIMEOUT seconds
                    printf("Receiving ACK timeout.\n");
                    cwnd = 1;
                    ssthreshold = cwnd / 2 > 2 ? cwnd / 2 : 2;
                    seq_num = wnd_left;
                    wnd_right = wnd_left + cwnd;
                    resend_flag = 1;
                    break;
                }
                else {
                    printf("Cannot receive ACK from server: other situation");
                    return 1;
                }
            } else { // receive ACK successfully
                printRecv(&pkt, 0);
                if (pkt.acknum >= wnd_left + 1) { // receive an in-order ACK, also could be a cumulated ACK
                    int move_step = pkt.acknum - wnd_left;
                    if (cwnd <= ssthreshold) {
                        cwnd += 1;
                    }
                    wnd_left = pkt.acknum;
                    wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd; // in my setting, wnd_right is somehow like the pointer pointing to the next packet after the current sending window, so need to add 1 here
                    ack_num = pkt.seqnum + 1;
                    printf("Move Congestion Window by %d Packet. Current window is [%d, %d]\n", move_step, wnd_left, wnd_right - 1);
                    j = 0;
                    break;
                }
                if (pkt.acknum == seq_num) {  // if all packets and ACKs have been received
                    ack_num = pkt.seqnum + 1;
                    if (cwnd > ssthreshold) {
                        cwnd += 1;
                    }
                    wnd_right = wnd_left + cwnd;
                    printf("All Packets in the last window have been received.\n");
                    j = 0;
                    break;
                }
                if (pkt.acknum == wnd_left) { // P1 lost, P2 received, send A1, A1 means P1 lost
                    if (j < 2) {
                        j += 1;
                    } else {
                        j = 0;
                        ack_num = pkt.seqnum + 1;
                        seq_num = wnd_left;
                        ssthreshold = cwnd / 2 > 2 ? cwnd / 2 : 2;
                        cwnd = ssthreshold + 3;
                        wnd_right = wnd_left + cwnd;
                        resend_flag = 1;
                        printf("Packet %d should lost. Begin Fast Retransmit\n", wnd_left);
                        break;
                    }
                }
            }
        }
        if (resend_flag != 1 && pkt.last == 1) {  // make sure the pkt is the received packet. Not the already sent pkt. Otherwise will exit abnormally
            break;
        }
    }
    fclose(fp);
    fclose(stdout);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

