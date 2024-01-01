#include "rtp.h"
#include "util.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#define MAX_LENGTH 1600
#define HEADER_LENGTH 11
#define MAX_DATA_LENGTH 1461
#define MAX_PACKET_NUM 7200
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

static int receiverSocket;
static struct sockaddr_in dstAddr;
static socklen_t addrLength = sizeof(dstAddr);

static char sendbuf[MAX_LENGTH];
static char recvbuf[MAX_LENGTH];

void print_packet(int flag)
{
    rtp_packet_t some_rtp_packet;
    if (flag == 0)
    {
        printf("\nSEND:\n");
        memcpy(&some_rtp_packet, sendbuf, HEADER_LENGTH);
    }
    else
    {
        printf("\nRECEIVE:\n");
        memcpy(&some_rtp_packet, recvbuf, HEADER_LENGTH);
    }
    printf("seq_num: %u\n", (some_rtp_packet.rtp).seq_num);
    printf("length: %d\n", (some_rtp_packet.rtp).length);
    printf("checksum: %u\n", (some_rtp_packet.rtp).checksum);
    printf("flags: %d ", (some_rtp_packet.rtp).flags);
    if ((some_rtp_packet.rtp).flags & RTP_SYN)
        printf("SYN ");
    if ((some_rtp_packet.rtp).flags & RTP_ACK)
        printf("ACK ");
    if ((some_rtp_packet.rtp).flags & RTP_FIN)
        printf("FIN ");
    printf("\n");
}

void send_buf(int buf_len)
{
    ssize_t b = sendto(receiverSocket, sendbuf, buf_len, 0, (struct sockaddr*)&dstAddr, addrLength);
    if (b == 0) {
        perror("socket Closed");
    }
    if (b < 0) {
        perror("Error in sendto");
    }
    // print_packet(0);
    // printf("%zd bytes sent\n", b);
    return;
}

uint16_t recv_buf(int timeout_ms)
{
    struct timeval timeout;
    fd_set readfds;

    FD_ZERO(&readfds);
    FD_SET(receiverSocket, &readfds);

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int selectResult = select(receiverSocket + 1, &readfds, NULL, NULL, &timeout);

    if (selectResult <= 0) {
        // printf("Timeout: No data received within %d seconds.\n", timeout_ms);
        return 0;
    } else {
        ssize_t b = recvfrom(receiverSocket, recvbuf, MAX_LENGTH, 0, (struct sockaddr*)&dstAddr, &addrLength);

        if (b == 0) {
            perror("socket Closed");
        }
        if (b < 0) {
            perror("Error in recvfrom");
        }
        // printf("%zd bytes received\n", b);
        // print_packet(1);
        return b;
    }
    return 0;
}

bool check_sumnum(rtp_packet_t* some_rtp_packet)
{
    uint32_t received_checksum = (some_rtp_packet -> rtp).checksum;
    (some_rtp_packet -> rtp).checksum = 0;
    if (received_checksum == compute_checksum(some_rtp_packet, HEADER_LENGTH + (some_rtp_packet -> rtp).length))
    {
        (some_rtp_packet -> rtp).checksum = received_checksum;
        return true;
    }
    else
    {
        (some_rtp_packet -> rtp).checksum = received_checksum;
        return false;
    }
}

bool check_length(rtp_packet_t* some_rtp_packet, uint16_t recv_length)
{
    if ((some_rtp_packet -> rtp).length <= MAX_DATA_LENGTH && (some_rtp_packet -> rtp).length + HEADER_LENGTH == recv_length)
        return true;
    else
        return false;
}

uint32_t establishConnection()
{
    uint16_t recv_length;

    // 1 -> 2 receive RTP_SYN 

    rtp_packet_t syn_packet;
    while (true)
    {
        if (!(recv_length = recv_buf(5000)))
        {
            close(receiverSocket);
            exit(1);
        }
        memcpy(&syn_packet, recvbuf, HEADER_LENGTH);
        if (!check_length(&syn_packet, recv_length) || !check_sumnum(&syn_packet) || !(syn_packet.rtp.flags == RTP_SYN))
            continue;
        else
            break;
    }

    // send RTP_SYNACK

    rtp_packet_t synack_packet;
    memset(&synack_packet, 0, sizeof(synack_packet));
    synack_packet.rtp.seq_num  = syn_packet.rtp.seq_num + 1;
    synack_packet.rtp.length = 0;
    synack_packet.rtp.flags = RTP_SYN | RTP_ACK;
    synack_packet.rtp.checksum = 0;
    synack_packet.rtp.checksum = compute_checksum(&synack_packet, HEADER_LENGTH);
    memcpy(sendbuf, &synack_packet, HEADER_LENGTH);

    rtp_packet_t ack_packet;    // should receive RTP_ACK
    bool recv_ack_flag = false;

    for (int cnt = 0; cnt < 50; cnt++)  // loop to send
    {
        send_buf(HEADER_LENGTH);
        // printf("%d\n", cnt);
        if (!(recv_length = recv_buf(100)))  // if not receive, send again
            continue;
        memcpy(&ack_packet, recvbuf, HEADER_LENGTH);
        if (!check_length(&ack_packet, recv_length) || !check_sumnum(&ack_packet)) // if not right, send again
            continue;
        if (!(ack_packet.rtp.flags == RTP_ACK))  // if not ACK, send again
            continue;
        if (ack_packet.rtp.seq_num != syn_packet.rtp.seq_num + 1)    // if seq_num not right, send again
            continue;
        recv_ack_flag = true;
        break;
    }

    if (!recv_ack_flag) // if not receive after 50 trials, exits
    {
        close(receiverSocket);
        exit(1);
    }

    return syn_packet.rtp.seq_num;
}

void GBN(const char *file_path, uint32_t syn_seq, int window_size)
{
    FILE *file = fopen(file_path, "wb");
    if (!file) {
        perror("Error opening file");
        close(receiverSocket);
        exit(1);
    }

    uint32_t expected_packet = 1;
    rtp_packet_t rtp_packet, ack_packet, finack_packet;
    uint16_t recv_length;
    while (true)
    {
        // receive in 5000ms
        // -> not receive anything: exits
        // -> receive wrong packet(length, countsum, flags, seq_num not in window range): throw away
        // -> receive duplicated packet(seq_num not expected but still within window range): resend ACK
        // -> receive: expected ++ & send ACK & write to file
        if (!(recv_length = recv_buf(5000)))
            break;
        
        memcpy(&rtp_packet, recvbuf, HEADER_LENGTH);
        if (!check_length(&rtp_packet, recv_length))
        {
            // printf("wrong packet0\n");
            continue;
        }

        memcpy(rtp_packet.payload, recvbuf + HEADER_LENGTH, rtp_packet.rtp.length);

        if (!check_sumnum(&rtp_packet))
        {
            // printf("wrong packet1\n");
            continue;
            // memset(&ack_packet, 0, sizeof(ack_packet));
            // ack_packet.rtp.seq_num = expected_packet + syn_seq;
            // ack_packet.rtp.length = 0;
            // ack_packet.rtp.flags = RTP_ACK; 
            // ack_packet.rtp.checksum = 0;
            // ack_packet.rtp.checksum = compute_checksum(&ack_packet, HEADER_LENGTH);
            // memcpy(sendbuf, &ack_packet, HEADER_LENGTH);
            // send_buf(HEADER_LENGTH);

        }
        else if (rtp_packet.rtp.flags == RTP_FIN && rtp_packet.rtp.seq_num == expected_packet + syn_seq)
        {
            memset(&finack_packet, 0, sizeof(finack_packet));
            finack_packet.rtp.seq_num = rtp_packet.rtp.seq_num;
            finack_packet.rtp.length = 0;
            finack_packet.rtp.flags = RTP_FIN | RTP_ACK; 
            finack_packet.rtp.checksum = 0;
            finack_packet.rtp.checksum = compute_checksum(&finack_packet, HEADER_LENGTH);
            memcpy(sendbuf, &finack_packet, HEADER_LENGTH);
            send_buf(HEADER_LENGTH);
            //fclose(file);
            break;
        }
        else if(!rtp_packet.rtp.flags)
        {
            if (rtp_packet.rtp.seq_num == expected_packet + syn_seq)
            {
                // printf("exact packet\n");
                fwrite(rtp_packet.payload, 1, rtp_packet.rtp.length, file);
                expected_packet ++;
            }
            else if (rtp_packet.rtp.seq_num >= expected_packet + syn_seq + window_size || rtp_packet.rtp.seq_num < expected_packet + syn_seq - window_size)
            {
                // printf("not in window range\n");
                continue;
            }
            // printf("within window range\n");
            memset(&ack_packet, 0, sizeof(ack_packet));
            ack_packet.rtp.seq_num = expected_packet + syn_seq;
            ack_packet.rtp.length = 0;
            ack_packet.rtp.flags = RTP_ACK; 
            ack_packet.rtp.checksum = 0;
            ack_packet.rtp.checksum = compute_checksum(&ack_packet, HEADER_LENGTH);
            memcpy(sendbuf, &ack_packet, HEADER_LENGTH);
            send_buf(HEADER_LENGTH);
        }

    }
    fclose(file);
    // sleep(2);
    return;
}

void SR(const char *file_path, uint32_t syn_seq, int window_size)
{
    FILE *file = fopen(file_path, "wb");
    if (!file) {
        perror("Error opening file");
        close(receiverSocket);
        exit(1);
    }

    uint32_t base = 1;
    
    char ** cached_data = (char **)malloc(sizeof(char *) * MAX_PACKET_NUM);
    uint16_t* data_length = (uint16_t*)malloc(MAX_PACKET_NUM * sizeof(uint16_t));
    memset(data_length, 0, MAX_PACKET_NUM * sizeof(uint16_t));

    rtp_packet_t rtp_packet, ack_packet, finack_packet;
    uint16_t recv_length;

    while (true)
    {
        // receive in 5000ms
        // -> not receive anything: exits
        // -> receive wrong packet(length, countsum, flags, seq_num not in range): throw away
        // -> receive packet seq in [base, base + size - 1]: send ACK
        //      -> if cached_data = NULL: set cached_data = packet
        //              -> if seq == base: write data & move window
        // -> receive packet seq in [base - size, base - 1]: send ACK
        if (!(recv_length = recv_buf(5000)))
            break;
        
        memcpy(&rtp_packet, recvbuf, HEADER_LENGTH);
        if (!check_length(&rtp_packet, recv_length))
        {
            continue;
        }

        uint32_t packet_num = rtp_packet.rtp.seq_num - syn_seq;

        memcpy(rtp_packet.payload, recvbuf + HEADER_LENGTH, rtp_packet.rtp.length);
        
        if (!check_sumnum(&rtp_packet))
        {
            continue;
        }
        else if (rtp_packet.rtp.flags == RTP_FIN && packet_num == base)
        {
            memset(&finack_packet, 0, sizeof(finack_packet));
            finack_packet.rtp.seq_num = rtp_packet.rtp.seq_num;
            finack_packet.rtp.length = 0;
            finack_packet.rtp.flags = RTP_FIN | RTP_ACK; 
            finack_packet.rtp.checksum = 0;
            finack_packet.rtp.checksum = compute_checksum(&finack_packet, HEADER_LENGTH);
            memcpy(sendbuf, &finack_packet, HEADER_LENGTH);
            send_buf(HEADER_LENGTH);
            break;
        }
        else if(!rtp_packet.rtp.flags)
        {
            if (packet_num + window_size < base || packet_num >= base + window_size)
            {
                continue;
            }

            // send ACK
            memset(&ack_packet, 0, sizeof(ack_packet));
            ack_packet.rtp.seq_num = rtp_packet.rtp.seq_num;
            ack_packet.rtp.length = 0;
            ack_packet.rtp.flags = RTP_ACK; 
            ack_packet.rtp.checksum = 0;
            ack_packet.rtp.checksum = compute_checksum(&ack_packet, HEADER_LENGTH);
            memcpy(sendbuf, &ack_packet, HEADER_LENGTH);
            send_buf(HEADER_LENGTH);

            if (packet_num >= base && packet_num < base + window_size)
            {
                if (data_length[packet_num] == 0)
                {
                    cached_data[packet_num] = malloc(sizeof(char) * rtp_packet.rtp.length);
                    data_length[packet_num] = rtp_packet.rtp.length;
                    memcpy(cached_data[packet_num], rtp_packet.payload, rtp_packet.rtp.length);
                    if (packet_num == base)
                    {
                        while (data_length[base] != 0)
                        {
                            fwrite(cached_data[base], 1, data_length[base], file);
                            free(cached_data[base]);
                            base++;
                        }
                    }
                }
            }
        }
    }

    free(cached_data);
    free(data_length);
    fclose(file);
    return;
}


int main(int argc, char **argv) {
    
    if (argc != 5) {
        LOG_FATAL("Usage: ./receiver [listen port] [file path] [window size] "
                  "[mode]\n");
    }

    // 命令行参数
    int listen_port = atoi(argv[1]);
    const char* file_path = argv[2];
    int window_size = atoi(argv[3]);
    int mode = atoi(argv[4]);

    // 创建socket
    receiverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (receiverSocket < 0) {
        close(receiverSocket);
        perror("Error creating socket");
    }

    // Set the SO_REUSEADDR option
    // int reuseAddr = 1;
    // if (setsockopt(receiverSocket, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
    //     close(receiverSocket);
    //     perror("setsockopt(SO_REUSEADDR) failed");
    //     exit(1);
    // }

    // Set up the address structure
    struct sockaddr_in lstAddr;
    memset(&lstAddr, 0, sizeof(lstAddr));
    lstAddr.sin_family = AF_INET;
    lstAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    lstAddr.sin_port = htons(listen_port);

    // Solve the problem "Bind failed: Address already in use"
    // int reuse = 1;
    // setsockopt(receiverSocket,SOL_SOCKET ,SO_REUSEADDR,(const char*)& reuse,sizeof(int));

    // Bind the socket
    if (bind(receiverSocket, (struct sockaddr*)&lstAddr, sizeof(lstAddr)) < 0) {
        close(receiverSocket);
        perror("Bind failed");
        return 0;
    }


    uint32_t syn_seq = establishConnection();

    if (mode == 0)
    {
        GBN(file_path, syn_seq, window_size);

    }
    else if (mode == 1)
    {
        SR(file_path, syn_seq, window_size);
    }
    
    // Solve the problem "Bind failed: Address already in use", set SO_REUSEADDR
    // int reuse = 0;
    // setsockopt(receiverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)& reuse, sizeof(int));

    close(receiverSocket);
    // LOG_DEBUG("Receiver: exiting...\n");
    return 0;
}
