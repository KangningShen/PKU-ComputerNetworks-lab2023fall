#include <iostream>
#include <stdio.h>
#include <string>
#include <cstring>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

#define MAX_LENGTH 4096
#define MAGIC_NUMBER_LENGTH 6
#define HEADER_LENGTH 12
#define type char
#define status char

const string myFTP = "\xc1\xa1\x10""ftp";

struct header{
    byte m_protocol[MAGIC_NUMBER_LENGTH]; /* protocol magic number (6 bytes) */
    type m_type;                          /* type (1 byte) */
    status m_status;                      /* status (1 byte) */
    uint32_t m_length;                    /* length (4 bytes) in Big endian*/
} __attribute__ ((packed));

void send_buf(int sock_fd, char* buffer, int buf_len)
{
    ssize_t ret = 0;
    while (ret < buf_len)
    {
        ssize_t b = send(sock_fd, buffer + ret, buf_len - ret, 0);
        if (b == 0) printf("socket Closed");
        if (b < 0) printf("Error ?");
        ret += b;
    }
}

void recv_buf(int sock_fd, char* buffer, int buf_len)
{
    ssize_t ret = 0;
    while (ret < buf_len)
    {
        ssize_t b = recv(sock_fd, buffer + ret, buf_len - ret, 0);
        if (b == 0) printf("socket Closed");
        if (b < 0) printf("Error ?");
        ret += b;
    }
}

int main(int argc, char ** argv) {
    // 定义serverSocket_fd
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_port = htons(atoi(argv[2]));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &addr.sin_addr);
    bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr));
    listen(serverSocket, 128);
    int client;

    // 缓冲区，用于send和recv通信
    char buf[MAX_LENGTH];
    char payload[MAX_LENGTH];

    // 响应报文
    header REPLY;
    memcpy(REPLY.m_protocol, myFTP.data(), myFTP.length());

    // 记录是否为连接状态
    int connected = 0;
    while (1)
    {
        if (!connected)
        {
            client = accept(serverSocket, nullptr, nullptr);
            connected = 1;
        }
        else
        {
            // 接收传过来的报文信息
            recv_buf(client, buf, HEADER_LENGTH);

            // 解析报文信息
            header msg;
            memcpy(&msg, buf, HEADER_LENGTH);

            switch (msg.m_type)
            {
                case '\xA1':     // -------------------------------> 1  OPEN_CONN_REQUEST
                    REPLY.m_type = '\xA2';  // OPEN_CONN_REPLY
                    REPLY.m_status = 1;
                    REPLY.m_length = htonl(HEADER_LENGTH);
                    memcpy(buf, &REPLY, HEADER_LENGTH);
                    send_buf(client, buf, HEADER_LENGTH);
                    break;
                case '\xA3':     // -------------------------------> 2  LIST_REQUEST
                {
                    string cmd = "ls"; 
                    FILE *file_list = popen(cmd.c_str(), "r");
                    int file_list_len = 0;
                    while(1)
                    {
                        int tmp_len = fread(payload + file_list_len, 1, 2048, file_list);
                        if (tmp_len <= 0)
                            break;
                        file_list_len += tmp_len;
                    }
                    *(payload + file_list_len) = '\0';
                    REPLY.m_type = '\xA4';  // LIST_REPLY
                    REPLY.m_length = htonl(HEADER_LENGTH + strlen(payload) + 1);
                    memcpy(buf, &REPLY, HEADER_LENGTH);
                    memcpy(buf + HEADER_LENGTH, payload, strlen(payload) + 1);
                    send_buf(client, buf, HEADER_LENGTH + strlen(payload) + 1);
                    break;
                }
                case '\xA5':    // -------------------------------> 3 GET_REQUEST
                {
                    char file_name[MAX_LENGTH];
                    recv_buf(client, file_name, ntohl(msg.m_length) - HEADER_LENGTH);
                    FILE* file_fd = fopen(file_name, "r");

                    REPLY.m_type = '\xA6';  // GET_REPLY
                    REPLY.m_length = htonl(HEADER_LENGTH);
                    if (file_fd == NULL)
                        REPLY.m_status = 0;
                    else 
                        REPLY.m_status = 1;
                    memcpy(buf, &REPLY, HEADER_LENGTH);
                    send_buf(client, buf, HEADER_LENGTH);
                    if (REPLY.m_status == 0)
                        break;

                    fseek(file_fd, 0, SEEK_END);
                    int file_size = ftell(file_fd);
                    fseek(file_fd, 0, SEEK_SET);
                    REPLY.m_type = '\xFF';  // FILE_DATA
                    REPLY.m_length = htonl(HEADER_LENGTH + file_size);
                    memcpy(buf, &REPLY, HEADER_LENGTH);
                    send_buf(client, buf, HEADER_LENGTH);
                    int nByte = 0;
                    while ((nByte = fread(buf, 1, 2048, file_fd)) > 0)
                        send_buf(client, buf, nByte);
                    fclose(file_fd);
                    break;
                }
                case '\xA7':    // -------------------------------> 4 PUT_REQUEST
                {
                    char file_name[MAX_LENGTH];
                    recv_buf(client, file_name, ntohl(msg.m_length) - HEADER_LENGTH);

                    REPLY.m_type = '\xA8';  // PUT_REPLY
                    REPLY.m_length = htonl(HEADER_LENGTH);
                    memcpy(buf, &REPLY, HEADER_LENGTH);
                    send_buf(client, buf, HEADER_LENGTH);

                    recv_buf(client, buf, HEADER_LENGTH);   // FILE_DATA
                    memcpy(&msg, buf, HEADER_LENGTH);
                    int file_size = ntohl(msg.m_length) - HEADER_LENGTH;
                    FILE* file_fd = fopen(file_name, "w");
                    while (file_size)
                    {
                        recv_buf(client, buf, min(2048, file_size));
                        fwrite(buf, 1, min(2048, file_size), file_fd);
                        file_size -= min(2048, file_size);
                    }
                    fclose(file_fd);
                    break;
                }
                case '\xA9':    // -------------------------------> 5 SHA_REQUEST
                {
                    char file_name[MAX_LENGTH];
                    recv_buf(client, file_name, ntohl(msg.m_length) - HEADER_LENGTH);
                    FILE* file_fd = fopen(file_name, "r");

                    REPLY.m_type = '\xAA';  // SHA_REPLY
                    REPLY.m_length = htonl(HEADER_LENGTH);
                    if (file_fd == NULL)
                        REPLY.m_status = 0;
                    else 
                        REPLY.m_status = 1;
                    memcpy(buf, &REPLY, HEADER_LENGTH);
                    send_buf(client, buf, HEADER_LENGTH);
                    if (REPLY.m_status == 0)
                        break;

                    char cmd[MAX_LENGTH] = "sha256sum ";
                    memcpy(cmd + 10, file_name, ntohl(msg.m_length) - HEADER_LENGTH);
                    FILE *file_list = popen(cmd, "r");
                    int file_list_len = 0;
                    while(1)
                    {
                        int tmp_len = fread(payload + file_list_len, 1, 2048, file_list);
                        if (tmp_len <= 0)
                            break;
                        file_list_len += tmp_len;
                    }
                    *(payload + file_list_len) = '\0';

                    REPLY.m_type = '\xFF';  // FILE_DATA
                    REPLY.m_length = htonl(HEADER_LENGTH + strlen(payload) + 1);
                    memcpy(buf, &REPLY, HEADER_LENGTH);
                    memcpy(buf + HEADER_LENGTH, payload, strlen(payload) + 1);
                    send_buf(client, buf, HEADER_LENGTH + strlen(payload) + 1);
                    break;
                }
                case '\xAB':    // -------------------------------> 6 QUIT_REQUEST
                    REPLY.m_type = '\xAC';  // QUIT_REPLY
                    REPLY.m_length = htonl(HEADER_LENGTH);
                    memcpy(buf, &REPLY, HEADER_LENGTH);
                    send_buf(client, buf, HEADER_LENGTH);
                    connected = 0;
                    break;
            }
        }

    }
    
    return 0;
}