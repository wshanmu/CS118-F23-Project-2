#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

#include <time.h>
#include <sys/errno.h>

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
    if (print_flag) {
        freopen("server_output_message.txt", "w", stdout); // redirect stdout to a file
        time(&start);
    }

    struct packet receive_buffer[100];
    for (int i = 0; i < 100; i++) {
        receive_buffer[i].ack = 1; // packet received always has ack = 0, so this indicates no those in-store packets
    }
    int store_len = 0;
    int store_wnd_right = 0, store_wnd_left = 0; // this is indicated by seq_num
    int store_wnd_start = 0;
    int i = 0; // for indexing the store window
    while (1) {
        if (recvfrom(listen_sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *) &client_addr_from, &addr_size) < 0) {
            if (errno == EAGAIN) {  // fail to receive ACK within TIMEOUT seconds
                if (expected_seq_num > 1) {
                    ack_pkt.acknum = expected_seq_num;
                    if(sendto(listen_sockfd, (void*) &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&client_addr_to, addr_size) < 0){
                        printf("Cannot send message to client");
                        return 1;
                    }
                    if (print_flag) { printf("Didn't wait for the expected packet. ACK probably lost.\n"); }
                    if (print_flag) { printSend(&ack_pkt, 1, 1);}
                }
                continue;
            }
            else { printf("Cannot receive packet from client: other situation"); return 1; }
        } else {  // successfully receive an packet
            if (print_flag) { printRecv(&buffer, 1);}
            if (buffer.seqnum < expected_seq_num) {
                if (print_flag) { printf("Receive an old packet.\n");}
                build_packet(&ack_pkt, seq_num_server, expected_seq_num, buffer.last, 1, PAYLOAD_SIZE, payload);
                if(sendto(listen_sockfd, (void*) &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&client_addr_to, addr_size) < 0){
                    printf("Cannot send message to client1"); return 1;
                }
                if (print_flag) { printSend(&ack_pkt, 0, 1);}
            } else if (buffer.seqnum == expected_seq_num) {
                // if receive in-order packet
                fwrite(buffer.payload, sizeof(char),buffer.length, fp);
                expected_seq_num += 1;
                if (print_flag) { printf("This is an in-order packet. Write Packet-%d.\n", buffer.seqnum);}
//                if (buffer.last == 1) {break;}

                // if have stored next several packets
                if (store_len > 0 && store_wnd_left == expected_seq_num) {
                    for (i = store_wnd_left; i <= store_wnd_right + 1; i++) {
                        if (!receive_buffer[i - store_wnd_start].ack) {
                            fwrite(receive_buffer[i - store_wnd_start].payload, sizeof(char),receive_buffer[i - store_wnd_start].length, fp);
                            receive_buffer[i - store_wnd_start].ack = 1;
                            expected_seq_num += 1;
                            store_len -= 1;
                            if (print_flag) { printf("Write in-store Packet-%d.\n", receive_buffer[i - store_wnd_start].seqnum);}
                        } else { // didn't store the packet
                            // update store_wnd_left
                            for (int j = i; j <= store_wnd_right; j++) {
                                if (!receive_buffer[j - store_wnd_start].ack) {
                                    store_wnd_left = receive_buffer[j - store_wnd_start].seqnum;
                                    if (print_flag) { printf("Update store_wnd_left to %d\n", store_wnd_left);}
                                    break;
                                }
                            }
                            // send a cumulative ACK
                            seq_num_server += 1;
                            build_packet(&ack_pkt, seq_num_server, expected_seq_num, 0, 1, PAYLOAD_SIZE, payload);
                            if(sendto(listen_sockfd, (void*) &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&client_addr_to, addr_size) < 0){
                                printf("Cannot send message to client2");
                                return 1;
                            }
                            if (print_flag) { printSend(&ack_pkt, 0, 1);}
                            break;
                        }
                    }
                    if (receive_buffer[i - 1 - store_wnd_start].last == 1) {
                        break;
                    }
                } else { // no in-store packet or the stored packets are not consequent to the current one
                    seq_num_server += 1;
                    build_packet(&ack_pkt, seq_num_server, expected_seq_num, buffer.last, 1, PAYLOAD_SIZE, payload);
                    if(sendto(listen_sockfd, (void*) &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&client_addr_to, addr_size) < 0){
                        printf("Cannot send message to client3");
                        return 1;
                    }
                    if (print_flag) { printSend(&ack_pkt, 0, 1);}
                    if (buffer.last == 1) {
                        break;
                    }
                }
            } else { // seq_num > expected, out-of-order packets
                if (print_flag) { printf("Receive out-of-order packet: %d.\n", buffer.seqnum);}
                if (store_len == 0) {
                    store_wnd_start = expected_seq_num;
                    receive_buffer[buffer.seqnum - store_wnd_start] = buffer;
                    store_wnd_left =  buffer.seqnum;
                    store_wnd_right = buffer.seqnum;
                    store_len += 1;
                } else {
                    if (receive_buffer[buffer.seqnum - store_wnd_start].ack) {  // if no store this packet before
                        receive_buffer[buffer.seqnum - store_wnd_start] = buffer;
                        if (buffer.seqnum > store_wnd_right){
                            store_wnd_right = buffer.seqnum;
                        }
                        if (buffer.seqnum < store_wnd_left){
                            store_wnd_left = buffer.seqnum;
                        }
                        store_len += 1;
                    }
                }
                if (print_flag) { printf("Current store window is [%d, %d]\n", store_wnd_left, store_wnd_right);}
                build_packet(&ack_pkt, seq_num_server, expected_seq_num, 0, 1, PAYLOAD_SIZE, payload); // because out-of-order, current ACK must not the last ACK
                if(sendto(listen_sockfd, (void*) &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*)&client_addr_to, addr_size) < 0){
                    printf("Cannot send message to client4");
                    return 1;
                }
                if (print_flag) { printSend(&ack_pkt, 0, 1);}
            }
        }
    }
    if (print_flag) {
        time(&end);
        double time_taken = end - start;
        printf("It took %.2f seconds.\n", time_taken);
    }


    fclose(fp);
    fclose(stdout);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}