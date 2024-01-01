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

//uint32_t htonl(uint32_t hostlong);
//uint32_t ntohl(uint32_t netlong);

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

int main() {
    // 定义clientSocket_fd
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;

    // 存放用户的命令行交互
    char line[MAX_LENGTH];
    char *token;
    const char blank[2] = " ";

    // 缓冲区，用于send和recv通信
    char buf[MAX_LENGTH];

    // 请求报文
    header REQUEST;
    memcpy(REQUEST.m_protocol, myFTP.data(), myFTP.length());
    char payload[MAX_LENGTH];

    // 收到的响应报文
    header msg;

    // 记录是否为连接状态
    int connected = 0;
    
    while (1)
    {
        printf("Client> ");
        cin.getline(line, 2048);
        token = strtok(line, blank);
        if (token == NULL) // ---------------------------------> NULL
            continue;
        else if (!strcmp(token, "quit")) // -------------------------------> 6 quit
        {
            if (connected)
            {
                REQUEST.m_type = '\xAB';    // QUIT_REQUEST
                REQUEST.m_length = htonl(HEADER_LENGTH);
                memcpy(buf, &REQUEST, HEADER_LENGTH);
                send_buf(clientSocket, buf, HEADER_LENGTH);

                recv_buf(clientSocket, buf, HEADER_LENGTH); // QUIT_REPLY
                close(clientSocket);
                connected = 0;
            }
            printf("Thank you.\n");
            return 0;
        }
        else if (!connected)            // ---------------------------------------> 1 open SERVER_IP SERVER_PORT
        {
            // 未连接时只能进行open操作
            if (strcmp(token, "open"))
                continue;
                
            token = strtok(NULL, blank); // <IP>
            inet_pton(AF_INET, token, &addr.sin_addr);

            token = strtok(NULL, blank); // <port>
            addr.sin_port = htons(atoi(token));

            clientSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (connect(clientSocket, (struct sockaddr *)&addr, sizeof(addr)) < 0) // 连接失败
            {
                printf("ERROR: Server connection rejected.\n");
                continue;
            }

            // 连接成功，已经建立TCP连接
            connected = 1;

            // 生成协议消息OPEN_CONN_REQUEST
            REQUEST.m_type = '\xA1';
            REQUEST.m_length = htonl(HEADER_LENGTH);

            // 协议消息复制到缓冲区
            memcpy(buf, &REQUEST, HEADER_LENGTH);

            // client向server发送OPEN_CONN_REQUEST
            send_buf(clientSocket, buf, HEADER_LENGTH);

            // client收到server发送的OPEN_CONN_REQUEST
            recv_buf(clientSocket, buf, HEADER_LENGTH);

            printf("Server connection accepted.\n");
            continue;
        }
        else if (!strcmp(token, "ls")) // -------------------------------> 2 ls
        {       
            REQUEST.m_type = '\xA3';    // LIST_REQUEST
            REQUEST.m_length = htonl(HEADER_LENGTH);
            memcpy(buf, &REQUEST, HEADER_LENGTH);
            send_buf(clientSocket, buf, HEADER_LENGTH);

            recv_buf(clientSocket, buf, HEADER_LENGTH); // LIST_REPLY
            memcpy(&msg, buf, HEADER_LENGTH);
            recv_buf(clientSocket, buf, ntohl(msg.m_length) - HEADER_LENGTH);

            printf("----- file list start -----\n");    // display the file list
            printf("%s", buf);
            printf("----- file list end -----\n");
            continue;
        }
        else if (!strcmp(token, "get")) // -------------------------------> 3 get FILE
        {
            token = strtok(NULL, blank);
            REQUEST.m_type = '\xA5';    // GET_REQUEST
            REQUEST.m_length = htonl(HEADER_LENGTH + strlen(token) + 1);
            memcpy(buf, &REQUEST, HEADER_LENGTH);
            memcpy(buf + HEADER_LENGTH, token, strlen(token) + 1);
            send_buf(clientSocket, buf, HEADER_LENGTH + strlen(token) + 1);

            recv_buf(clientSocket, buf, HEADER_LENGTH); // GET_REPLY
            memcpy(&msg, buf, HEADER_LENGTH);
            if (msg.m_status == 0)
            {
                printf("ERROR: No such file.\n");
                continue;
            }

            recv_buf(clientSocket, buf, HEADER_LENGTH); // FILE_DATA
            memcpy(&msg, buf, HEADER_LENGTH);
            int file_size = ntohl(msg.m_length) - HEADER_LENGTH;

            FILE* file_fd = fopen(token, "w");

            while (file_size)
            {
                recv_buf(clientSocket, buf, min(2048, file_size));
                fwrite(buf, 1, min(2048, file_size), file_fd);
                file_size -= min(2048, file_size);
            }

            fclose(file_fd);

            continue;
        }
        else if (!strcmp(token, "put")) // -------------------------------> 4 put FILE
        {
            token = strtok(NULL, blank);
            FILE* file_fd = fopen(token, "r");
            if (file_fd == NULL)
            {
                printf("File doesn't exist.\n");
                continue;
            }

            REQUEST.m_type = '\xA7';    // PUT_REQUEST
            REQUEST.m_length = htonl(12 + strlen(token) + 1);
            memcpy(buf, &REQUEST, HEADER_LENGTH);
            memcpy(buf + HEADER_LENGTH, token, strlen(token) + 1);
            send_buf(clientSocket, buf, HEADER_LENGTH + strlen(token) + 1);

            recv_buf(clientSocket, buf, HEADER_LENGTH); // PUT_REPLY

            fseek(file_fd, 0, SEEK_END);
            int file_size = ftell(file_fd);
            fseek(file_fd, 0, SEEK_SET);
            REQUEST.m_type = '\xFF';    // FILE_DATA
            REQUEST.m_length = htonl(HEADER_LENGTH + file_size);
            memcpy(buf, &REQUEST, HEADER_LENGTH);
            send_buf(clientSocket, buf, HEADER_LENGTH);
            int nByte = 0;
            while ((nByte = fread(buf, 1, 2048, file_fd)) > 0)
                send_buf(clientSocket, buf, nByte);
            fclose(file_fd);
            continue;
        }

        else if (!strcmp(token, "sha256")) // -------------------------------> 5 sha256 FILE
        {
            token = strtok(NULL, blank);
            REQUEST.m_type = '\xA9';    // SHA_REQUEST
            REQUEST.m_length = htonl(12 + strlen(token) + 1);
            memcpy(buf, &REQUEST, HEADER_LENGTH);
            memcpy(buf + HEADER_LENGTH, token, strlen(token) + 1);
            send_buf(clientSocket, buf, HEADER_LENGTH + strlen(token) + 1);

            recv_buf(clientSocket, buf, HEADER_LENGTH); // SHA_REPLY
            memcpy(&msg, buf, HEADER_LENGTH);
            if (msg.m_status == 0)
            {
                printf("ERROR: No such file.\n");
                continue;
            }

            recv_buf(clientSocket, buf, HEADER_LENGTH); // FILE_DATA
            memcpy(&msg, buf, HEADER_LENGTH);
            recv_buf(clientSocket, buf, ntohl(msg.m_length) - HEADER_LENGTH);

            printf("----- file list start -----\n");    // display the file sha256 checksum
            printf("%s", buf);
            printf("----- file list end -----\n");

            continue;
        }
    }
    return 0;
}