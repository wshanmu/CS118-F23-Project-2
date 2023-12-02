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
//    printf("Packet number is: %d.\n", packet_num);

    // partition the file into small segments and send segments to the server
    int packet_len = PAYLOAD_SIZE;
    int largest_seq_num = seq_num + packet_num - 1;
    int cwnd = 1;
    float cwnd_f = 1;
    int wnd_left = seq_num;
    int wnd_right = wnd_left + cwnd;
    int ssh = 5;
    int j = 0; // for fast retransmission
    int fr_flag = 0; // FR flag
    while (1) {
//        printf("Begin Sending Packet in Window: [%d, %d]\n", wnd_left, wnd_right-1);
        for (; seq_num < wnd_right; seq_num++) {   // send all packets in the congestion window
            if (seq_num == largest_seq_num) {
                packet_len = file_size - (seq_num) * PAYLOAD_SIZE;
                last = 1;
//                printf("Going to send the last packet.\n");
            } else {
                packet_len = PAYLOAD_SIZE;
                last = 0;
            }
            strncpy(buffer, file_content + (seq_num) * PAYLOAD_SIZE, packet_len);
            build_packet(&pkt, seq_num, ack_num, last, ack, packet_len, buffer);
            if(sendto(send_sockfd, (void*) &pkt, sizeof(pkt), 0, (struct sockaddr*)&server_addr_to, addr_size) < 0){
                printf("Cannot send file segment to server");
                return 1;
            }
//            printSend(&pkt, 0, 0);
//            usleep(100);
        }

        while (1) {
            if(recvfrom(listen_sockfd, (void*) &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&server_addr_from, &addr_size) < 0){   // wait for ACK, ack_num should be seq_num++
                if (errno == EAGAIN) {  // fail to receive ACK within TIMEOUT seconds
//                    printf("Receiving ACK timeout.\n");
                    cwnd = 1;
                    ssh = cwnd/2 > 2? cwnd/2 : 2;
                    seq_num = wnd_left;
                    wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd; // in my setting, wnd_right is somehow like the pointer pointing to the next packet after the current sending window, so need to add 1 here
                    break;
                }
                else {
                    printf("Cannot receive ACK from server: other situation"); return 1;
                }
            } else { // receive ACK successfully
//                printRecv(&ack_pkt, 0);
                int move_step = 0;
                if (ack_pkt.acknum >= wnd_left + 1) { // receive an in-order ACK, also could be a cumulated ACK
                    if (fr_flag) {
                        fr_flag = 0;
                        cwnd = ssh + 1;
//                        printf("New ACK received, end FR.\n");
                    } else {
                        move_step = ack_pkt.acknum - wnd_left;
                        if (cwnd <= ssh) {
                            cwnd += 1;
//                            printf("Slow Start. cwnd+=1\n");
                        } else {
                            cwnd_f = cwnd + 1 / cwnd;
                            cwnd = (int) cwnd_f;
//                            printf("Congestion Avoidance.\n");
                        }
                    }
                    wnd_left = ack_pkt.acknum;
                    wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd;
                    ack_num = ack_pkt.seqnum + 1;
//                    printf("Move Congestion Window by %d Packet. Current window is [%d, %d]\n", move_step, wnd_left, wnd_right - 1);
                    break;
                }
                if (ack_pkt.acknum == seq_num) {  // if all packets and ACKs have been received
                    ack_num = ack_pkt.seqnum + 1;
                    cwnd += 1;
                    wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd;
//                    printf("All Packets have been received.\n");
                    break;
                }
                if (ack_pkt.acknum == wnd_left) { // P1 lost, P2 received, send A1, A1 means P1 lost
                    if (!fr_flag) {
                        j += 1;
                        if (j == 3) {
                            j = 0;
                            fr_flag = 1;
                            ssh = cwnd / 2 > 2 ? cwnd / 2 : 2;
                            cwnd = ssh + 3;
                            ack_num = ack_pkt.seqnum + 1;
                            seq_num = wnd_left;
                            wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd;
//                            printf("Packet %d should lost.\n", wnd_left);
                            break;
                        }
                    } else { // 4th 5th dup ACK, Fast Recovery
                        cwnd += 1;
                        wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd;
//                        printf("One more duplicated packet, Fast Recovery.\n");
                        break;
                    }
                }
            }
        }
        if (ack_pkt.last == 1) {
            break;
        }
    }
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

