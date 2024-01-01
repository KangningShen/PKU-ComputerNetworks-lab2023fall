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
#define packet_size ((i == num_packets) ? (file_size % MAX_DATA_LENGTH) : MAX_DATA_LENGTH)
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

static int senderSocket;
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
    ssize_t b = sendto(senderSocket, sendbuf, buf_len, 0, (struct sockaddr*)&dstAddr, addrLength);
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
    FD_SET(senderSocket, &readfds);

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int selectResult = select(senderSocket + 1, &readfds, NULL, NULL, &timeout);

    if (selectResult <= 0) {
        // printf("Timeout: No data received within %d seconds.\n", timeout_ms);
        return 0;
    } else {
        ssize_t b = recvfrom(senderSocket, recvbuf, MAX_LENGTH, 0, (struct sockaddr*)&dstAddr, &addrLength);

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
    srand(time(NULL));

    // 1 -> 2 send RTP_SYN

    rtp_packet_t syn_packet;
    memset(&syn_packet, 0, sizeof(syn_packet));
    syn_packet.rtp.seq_num  = rand();
    syn_packet.rtp.length = 0;
    syn_packet.rtp.flags = RTP_SYN;
    syn_packet.rtp.checksum = 0;
    syn_packet.rtp.checksum = compute_checksum(&syn_packet, HEADER_LENGTH);
    memcpy(sendbuf, &syn_packet, HEADER_LENGTH);

    rtp_packet_t synack_packet; // should receive RTP_SYNACK
    bool recv_synack_flag = false;
    uint16_t recv_length;

    for (int cnt = 0; cnt < 50; cnt++)  // Loop to send
    {
        send_buf(HEADER_LENGTH);
        // printf("%d\n", cnt);
        if (!(recv_length = recv_buf(100)))  // if not receive, send again
            continue;
        memcpy(&synack_packet, recvbuf, HEADER_LENGTH);
        if (!check_length(&synack_packet, recv_length) || !check_sumnum(&synack_packet))  // if not right, send again
            continue;
        if (!(synack_packet.rtp.flags == (RTP_ACK | RTP_SYN)))   // if not SYNACK, send again
            continue;
        if (synack_packet.rtp.seq_num != syn_packet.rtp.seq_num + 1)    // if seq_num not right, send again
            continue;
        recv_synack_flag = true;
        break;
    }

    if (!recv_synack_flag)  // if not receive after 50 trials, exits
    {
        close(senderSocket);
        exit(1);
    }

    // 2 -> 3 send ACK

    rtp_packet_t ack_packet;
    memset(&ack_packet, 0, sizeof(ack_packet));
    ack_packet.rtp.seq_num  = syn_packet.rtp.seq_num + 1;
    ack_packet.rtp.length = 0;
    ack_packet.rtp.flags = RTP_ACK;
    ack_packet.rtp.checksum = 0;
    ack_packet.rtp.checksum = compute_checksum(&ack_packet, HEADER_LENGTH);
    memcpy(sendbuf, &ack_packet, HEADER_LENGTH);

    for (int cnt = 0; cnt < 50; cnt++)  // Loop to send
    {
        send_buf(HEADER_LENGTH);
        //printf("%d\n", cnt);
        if (!(recv_length = recv_buf(2000)))  // if not receive, succeed
            break;
    }

    return syn_packet.rtp.seq_num;
}

uint32_t GBN(const char *file_path, uint32_t syn_seq, int window_size)
{
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        close(senderSocket);
        perror("Error opening file");
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *file_buffer = (char *)malloc(file_size);
    if (!file_buffer) {
        close(senderSocket);
        perror("Error allocating memory");
        fclose(file);
        exit(1);
    }

    if (fread(file_buffer, 1, file_size, file) != file_size) {
        perror("Error reading file");
        close(senderSocket);
        fclose(file);
        free(file_buffer);
        exit(1);
    }

    fclose(file);

    uint32_t num_packets = (file_size + MAX_DATA_LENGTH - 1) / MAX_DATA_LENGTH;
    
    uint32_t base = 1;  // Base sequence number of the window
    uint32_t window_end = min(num_packets, base + window_size - 1);

    clock_t start = clock();
    rtp_packet_t rtp_packet, ack_packet;
    uint16_t recv_length;
    while (true)
    {
        // receive in 100ms
        // -> not receive anything: resend every packets in current window & set time to 0
        // -> receive wrong packet(countsum, flag, seq_num out of range): throw away
        //             -> if time out, resend every packets in current window & set time to 0
        // -> receive right packet: set receive_ack to 1, modify base and set time to 0
        if (!(recv_length = recv_buf(100)))
        {
            for (uint32_t i = base; i <= window_end; i++)
            {
                memset(&rtp_packet, 0, sizeof(rtp_packet));
                rtp_packet.rtp.seq_num = i + syn_seq;
                rtp_packet.rtp.length = packet_size;
                rtp_packet.rtp.flags = 0; 
                rtp_packet.rtp.checksum = 0;
                memcpy(rtp_packet.payload, file_buffer + (i - 1) * MAX_DATA_LENGTH, packet_size);
                rtp_packet.rtp.checksum = compute_checksum(&rtp_packet, HEADER_LENGTH + packet_size);
                memcpy(sendbuf, &rtp_packet, HEADER_LENGTH + packet_size);
                send_buf(HEADER_LENGTH + packet_size);
                // send packet i
            }
            start = clock();
        }
        else
        {
            memcpy(&ack_packet, recvbuf, HEADER_LENGTH);

            uint32_t packet_seq = ack_packet.rtp.seq_num - syn_seq - 1;
            
            if (!check_length(&ack_packet, recv_length) || !check_sumnum(&ack_packet) || !(ack_packet.rtp.flags == RTP_ACK) || packet_seq < base || packet_seq > window_end)
            {
                if (((double)clock() - (double)start) / CLOCKS_PER_SEC > 0.1)
                {
                    for (uint32_t i = base; i <= window_end; i++)
                    {
                        memset(&rtp_packet, 0, sizeof(rtp_packet));
                        rtp_packet.rtp.seq_num = i + syn_seq;
                        rtp_packet.rtp.length = packet_size;
                        rtp_packet.rtp.flags = 0; 
                        rtp_packet.rtp.checksum = 0;
                        memcpy(rtp_packet.payload, file_buffer + (i - 1) * MAX_DATA_LENGTH, packet_size);
                        rtp_packet.rtp.checksum = compute_checksum(&rtp_packet, HEADER_LENGTH + packet_size);
                        memcpy(sendbuf, &rtp_packet, HEADER_LENGTH + packet_size);
                        send_buf(HEADER_LENGTH + packet_size);
                    }
                    start = clock();
                    // set time to 0
                }
            }

            else
            {
                base = packet_seq + 1;
                if (base == num_packets + 1)
                    break;
                uint32_t last_window = window_end;
                window_end = min(num_packets, base + window_size - 1);
                for (uint32_t i = last_window + 1; i <= window_end; i++)
                {
                    memset(&rtp_packet, 0, sizeof(rtp_packet));
                    rtp_packet.rtp.seq_num = i + syn_seq;
                    rtp_packet.rtp.length = packet_size;
                    rtp_packet.rtp.flags = 0; 
                    rtp_packet.rtp.checksum = 0;
                    memcpy(rtp_packet.payload, file_buffer + (i - 1) * MAX_DATA_LENGTH, packet_size);
                    rtp_packet.rtp.checksum = compute_checksum(&rtp_packet, HEADER_LENGTH + packet_size);
                    memcpy(sendbuf, &rtp_packet, HEADER_LENGTH + packet_size);
                    send_buf(HEADER_LENGTH + packet_size);
                }
                start = clock();
            }
        }
    }
    free(file_buffer);
    return syn_seq + num_packets;
}

uint32_t SR(const char *file_path, uint32_t syn_seq, int window_size)
{
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        close(senderSocket);
        perror("Error opening file");
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *file_buffer = (char *)malloc(file_size);
    if (!file_buffer) {
        close(senderSocket);
        perror("Error allocating memory");
        fclose(file);
        exit(1);
    }

    if (fread(file_buffer, 1, file_size, file) != file_size) {
        perror("Error reading file");
        close(senderSocket);
        fclose(file);
        free(file_buffer);
        exit(1);
    }

    fclose(file);

    uint32_t num_packets = (file_size + MAX_DATA_LENGTH - 1) / MAX_DATA_LENGTH;
    
    uint32_t base = 1;  // Base sequence number of the window
    uint32_t window_end = min(num_packets, base + window_size - 1);

    bool* receive_ack = (bool*)malloc((num_packets + 1) * sizeof(bool));
    memset(receive_ack, 0, (num_packets + 1) * sizeof(bool));

    clock_t start = clock();
    rtp_packet_t rtp_packet, ack_packet;
    uint16_t recv_length;

    while (true)
    {
        // receive in 100 ms
        // -> not receive anything: resend unACKed packets in current window & set time to 0
        // -> receive wrong packet(countsum, flag, seq_num out of range): throw away
        //             -> if time out, resend unACKed packets in current window & set time to 0
        // -> receive right packet: set receive_ack to 1
        //             -> if packet seq == base, move window, send new packets, set time to 0
        //             -> else if time out, resend unACKed packets in current window & set time to 0

        if (!(recv_length = recv_buf(100)))
        {
            for (uint32_t i = base; i <= window_end; i++)
            {
                if (receive_ack[i])
                    continue;
                memset(&rtp_packet, 0, sizeof(rtp_packet));
                rtp_packet.rtp.seq_num = i + syn_seq;
                rtp_packet.rtp.length = packet_size;
                rtp_packet.rtp.flags = 0; 
                rtp_packet.rtp.checksum = 0;
                memcpy(rtp_packet.payload, file_buffer + (i - 1) * MAX_DATA_LENGTH, packet_size);
                rtp_packet.rtp.checksum = compute_checksum(&rtp_packet, HEADER_LENGTH + packet_size);
                memcpy(sendbuf, &rtp_packet, HEADER_LENGTH + packet_size);
                send_buf(HEADER_LENGTH + packet_size);
                // send packet i
            }
            start = clock();
        }
        else
        {
            memcpy(&ack_packet, recvbuf, HEADER_LENGTH);
            uint32_t packet_seq = ack_packet.rtp.seq_num - syn_seq;
            if (!check_length(&ack_packet, recv_length) || !check_sumnum(&ack_packet) || !(ack_packet.rtp.flags == RTP_ACK) || packet_seq < base || packet_seq > window_end)
            {
                if (((double)clock() - (double)start) / CLOCKS_PER_SEC > 0.1)
                {
                    for (uint32_t i = base; i <= window_end; i++)
                    {
                        if (receive_ack[i])
                            continue;
                        memset(&rtp_packet, 0, sizeof(rtp_packet));
                        rtp_packet.rtp.seq_num = i + syn_seq;
                        rtp_packet.rtp.length = packet_size;
                        rtp_packet.rtp.flags = 0; 
                        rtp_packet.rtp.checksum = 0;
                        memcpy(rtp_packet.payload, file_buffer + (i - 1) * MAX_DATA_LENGTH, packet_size);
                        rtp_packet.rtp.checksum = compute_checksum(&rtp_packet, HEADER_LENGTH + packet_size);
                        memcpy(sendbuf, &rtp_packet, HEADER_LENGTH + packet_size);
                        send_buf(HEADER_LENGTH + packet_size);
                        // send packet i
                    }
                    start = clock();
                    // set time to 0
                }
            }
            else
            {
                receive_ack[packet_seq] = 1;
                if (packet_seq == base)
                {
                    while (base <= window_end && receive_ack[base])
                        base++;
                    if (base == num_packets + 1)
                        break;
                    int last_window = window_end;
                    window_end = min(num_packets, base + window_size - 1);
                    for (uint32_t i = last_window + 1; i <= window_end; i++)
                    {
                        memset(&rtp_packet, 0, sizeof(rtp_packet));
                        rtp_packet.rtp.seq_num = i + syn_seq;
                        rtp_packet.rtp.length = packet_size;
                        rtp_packet.rtp.flags = 0; 
                        rtp_packet.rtp.checksum = 0;
                        memcpy(rtp_packet.payload, file_buffer + (i - 1) * MAX_DATA_LENGTH, packet_size);
                        rtp_packet.rtp.checksum = compute_checksum(&rtp_packet, HEADER_LENGTH + packet_size);
                        memcpy(sendbuf, &rtp_packet, HEADER_LENGTH + packet_size);
                        send_buf(HEADER_LENGTH + packet_size);
                    }
                    start = clock();
                }
                else if (((double)clock() - (double)start) / CLOCKS_PER_SEC > 0.1)
                {
                    for (uint32_t i = base; i <= window_end; i++)
                    {
                        if (receive_ack[i])
                            continue;
                        memset(&rtp_packet, 0, sizeof(rtp_packet));
                        rtp_packet.rtp.seq_num = i + syn_seq;
                        rtp_packet.rtp.length = packet_size;
                        rtp_packet.rtp.flags = 0; 
                        rtp_packet.rtp.checksum = 0;
                        memcpy(rtp_packet.payload, file_buffer + (i - 1) * MAX_DATA_LENGTH, packet_size);
                        rtp_packet.rtp.checksum = compute_checksum(&rtp_packet, HEADER_LENGTH + packet_size);
                        memcpy(sendbuf, &rtp_packet, HEADER_LENGTH + packet_size);
                        send_buf(HEADER_LENGTH + packet_size);
                        // send packet i
                    }
                    start = clock();
                    // set time to 0
                }
            }
        }

    }

    free(file_buffer);
    free(receive_ack);
    return syn_seq + num_packets;
}

void disconnect(uint32_t fin_seq)
{
    rtp_packet_t fin_packet, finack_packet;
    memset(&fin_packet, 0, sizeof(fin_packet));
    fin_packet.rtp.seq_num = fin_seq;
    fin_packet.rtp.length = 0;
    fin_packet.rtp.flags = RTP_FIN;
    fin_packet.rtp.checksum = 0;
    fin_packet.rtp.checksum = compute_checksum(&fin_packet, HEADER_LENGTH);
    memcpy(sendbuf, &fin_packet, HEADER_LENGTH);
    
    uint16_t recv_length;

    for (int cnt = 0; cnt < 50; cnt++)  // Loop to send
    {
        send_buf(HEADER_LENGTH);
        //printf("%d\n", cnt);
        if (!(recv_length = recv_buf(100)))  // if not receive, send again
            continue;

        memcpy(&finack_packet, recvbuf, HEADER_LENGTH);
        
        if (!check_length(&finack_packet, recv_length) || !check_sumnum(&finack_packet))  // if not right, send again
            continue;
            
        if (!(finack_packet.rtp.flags == (RTP_ACK | RTP_FIN)))   // if not SYNACK, send again
            continue;
            
        if (finack_packet.rtp.seq_num != fin_seq)    // if seq_num not right, send again
            continue;
            
        break;
    }
    return;
}

int main(int argc, char **argv) {
    
    if (argc != 6) {
        LOG_FATAL("Usage: ./sender [receiver ip] [receiver port] [file path] "
                  "[window size] [mode]\n");
    }

    // 命令行参数
    const char* receiver_ip = argv[1];
    int receiver_port = atoi(argv[2]);
    const char* file_path = argv[3];
    int window_size = atoi(argv[4]);
    int mode = atoi(argv[5]);

    // 创建socket
    senderSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (senderSocket < 0) {
        perror("Error creating socket");
    }

    // 构造receiver地址
    memset(&dstAddr, 0, sizeof(dstAddr));
    dstAddr.sin_family = AF_INET;
    if (inet_pton(AF_INET, receiver_ip, &dstAddr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(senderSocket);
        return 0;
    }
    dstAddr.sin_port = htons(receiver_port);

    uint32_t syn_seq = establishConnection();

    uint32_t fin_seq = 0;

    if (mode == 0)
    {
        fin_seq = GBN(file_path, syn_seq, window_size) + 1;
    }
    else if (mode == 1)
    {
        fin_seq = SR(file_path, syn_seq, window_size) + 1;
    }

    disconnect(fin_seq);

    close(senderSocket);
    // LOG_DEBUG("Sender: exiting...\n");
    return 0;
}
