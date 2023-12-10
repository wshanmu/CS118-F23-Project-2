#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
//#include <time.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <signal.h>

#include "utils.h"

void handle_alarm(int sig) {
    // Signal handler functionality
    printf("Receiving ACK timeout for the last sending window.\n");
}


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

//    // set timeout interval for recvfrom function
//    tv.tv_sec = TIMEOUT;
//    tv.tv_usec = TIMEOUT_MS;
//    if (setsockopt(listen_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
//        perror("set socket failed");
//        close(listen_sockfd);
//        return 1;
//    }

    struct sigaction sa;
    sa.sa_handler = handle_alarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval timer;
    timer.it_value.tv_sec = 0; // Seconds
    timer.it_value.tv_usec = TIMEOUT_MS; // Microseconds
    timer.it_interval.tv_sec = 0; // Seconds for repeating
    timer.it_interval.tv_usec = 0; // Microseconds for repeating



    // Open file for reading
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }
    if (print_flag) { freopen("client_output_message.txt", "w", stdout); } // redirect stdout to a file

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
    if (print_flag) { printf("Packet number is: %d.\n", packet_num);}

    // partition the file into small segments and send segments to the server
    int packet_len = PAYLOAD_SIZE;
    int largest_seq_num = seq_num + packet_num - 1;
    int cwnd = 1;
    int wnd_left = seq_num;
    int wnd_right = wnd_left + cwnd;
    int j = 0; // for fast retransmission
    int fr_flag = 0; // FR flag
    int fast_recovery = 0;
    int move_step = 0;
    int sent_largest_seq = -1;
    int cnt = 0; // for congestion avoidance

    // parameters
    int ssh = 18;
    double degrade_factor = 0.85;
    double degrade_factor_timeout = 0.7;
    int dup_num = 2;

    while (1) {
        if (print_flag) { printf("Begin Sending Packet in Window: [%d, %d]\n", wnd_left, wnd_right-1); }
        for (; seq_num < wnd_right; seq_num++) {   // send all packets in the congestion window
            if (seq_num > sent_largest_seq) {
                if (seq_num == largest_seq_num) {
                    packet_len = file_size - (seq_num) * PAYLOAD_SIZE;
                    last = 1;
                    if (print_flag) { printf("Going to send the last packet.\n");}
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
                if (print_flag) { printSend(&pkt, 0, 0);}
                if (seq_num > sent_largest_seq) {
                    sent_largest_seq = seq_num;
                }
            }
        }
        setitimer(ITIMER_REAL, &timer, NULL);
        while (1) {
            if(recvfrom(listen_sockfd, (void*) &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&server_addr_from, &addr_size) < 0){   // wait for ACK, ack_num should be seq_num++
                if (errno == EINTR) {  // fail to receive ACK within TIMEOUT seconds
                    if (print_flag) { printf("Receiving ACK timeout. Current cwnd: %d\n", cwnd);}
                    ssh = degrade_factor_timeout * cwnd > 2 ? degrade_factor_timeout * cwnd : 2;
                    cwnd = 1;
                    cnt = 0;
                    seq_num = wnd_left;
                    wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd; // in my setting, wnd_right is somehow like the pointer pointing to the next packet after the current sending window, so need to add 1 here

                    // retransmit the lost packet
                    if (seq_num == largest_seq_num) {
                        packet_len = file_size - (seq_num) * PAYLOAD_SIZE;
                        last = 1;
                        if (print_flag) { printf("Going to resend the last packet.\n");}
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
                    j = 0;
                    fr_flag = 1;
                    if (print_flag) { printSend(&pkt, 1, 0); }
                    break;
                }
                else {
                    printf("Cannot receive ACK from server: other situation"); return 1;
                }
            } else { // receive ACK successfully
                if (print_flag) { printRecv(&ack_pkt, 0); }
                if (ack_pkt.acknum > wnd_left) { // receive an in-order ACK, also could be a cumulated ACK
                    if (fr_flag) {
                        j = 0;
                        fr_flag = 0;
                        cwnd = ssh + 1 > cwnd ? ssh + 1 : cwnd;
                        if (print_flag) { printf("New ACK received, end FR. Current ssh:%d, cwnd: %d\n", ssh, cwnd);}
                        wnd_left = ack_pkt.acknum;
                        wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd;
                    } else {
//                        move_step = ack_pkt.acknum - wnd_left;
                        if (cwnd <= ssh) {
                            cwnd += 1;
                            if (print_flag) { printf("Slow Start. cwnd+=1\n");}
                        } else {
                            int temp_cwnd = cwnd; //for debug
                            cnt += 1;

                            if (cnt >= cwnd) {  // first fix
                                cwnd += 1;
                                cnt = 0;
                            }

                            if (print_flag) { printf("Congestion Avoidance. Previous cwnd: %d, Current cwnd: %d, cnt: %d\n", temp_cwnd, cwnd, cnt);}
                        }
                        wnd_left = ack_pkt.acknum;
                        wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd;
                        ack_num = ack_pkt.seqnum + 1;
                    }
                    if (print_flag) { printf("Move Congestion Window by %d Packet. Current window is [%d, %d]\n", move_step, wnd_left, wnd_right - 1);}
                    break;
                }
                if (ack_pkt.acknum == wnd_left) { // P1 lost, P2 received, send A1, A1 means P1 lost
                    if (!fr_flag) {
                        j += 1;
                        if (j == dup_num) { // three duplicate ACKs
                            if (print_flag) { printf("Packet %d should lost. Current cwnd: %d\n", wnd_left, cwnd); }
                            j = 0;
                            fr_flag = 1;
                            fast_recovery = 1;
                            ssh = degrade_factor * cwnd > 2? degrade_factor * cwnd : 2;

                            cwnd = cwnd/2 + 1;  // second fix
                            cnt = 0;

                            ack_num = ack_pkt.seqnum + 1;
                            seq_num = wnd_left;
                            wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd;

                            if (seq_num == largest_seq_num) {
                                packet_len = file_size - (seq_num) * PAYLOAD_SIZE;
                                last = 1;
                                if (print_flag) { printf("Going to send the last packet.\n"); }
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
                            if (print_flag) { printSend(&pkt, 1, 0);}
                        }
                    } else { // 4th 5th dup ACK, Fast Recovery
                        cwnd += 1;
                        wnd_right = (wnd_left + cwnd > largest_seq_num)? largest_seq_num + 1 : wnd_left + cwnd;
                        if (print_flag) { printf("One more duplicated packet, Fast Recovery.\n"); }
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

