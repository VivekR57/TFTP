#include "tftp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PORT 6969
#define SERVER_IP "127.0.0.1"

void handle_client(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, tftp_packet *packet);
int main()
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    tftp_packet packet;

    printf("Server is waiting...\n");

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("TFTP Server listening on port %d...\n", PORT);

    while (1)
    {
        int n = recvfrom(sockfd, &packet, sizeof(packet), 0,
                         (struct sockaddr *)&client_addr, &client_len);
        if (n < 0)
        {
            perror("Receive failed");
            continue;
        }

        handle_client(sockfd, client_addr, client_len, &packet);
    }

    close(sockfd);
    return 0;
}

void handle_client(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, tftp_packet *packet)
{
    char *filename = packet->body.request.filename;
    if (ntohs(packet->opcode) == WRQ)
    {

        printf("Received write request for file: %s\n", filename);

        struct stat file_stat;
        int fd;

        // ✅ Check if the file exists using stat()
        if (stat(filename, &file_stat) == 0)
        {
            // ✅ File exists, rename old file and create a new one
            char backup_filename[300];
            snprintf(backup_filename, sizeof(backup_filename), "old_%s", filename);
            rename(filename, backup_filename);
            printf("Existing file renamed to %s\n", backup_filename);
        }

        // ✅ Create new empty file for writing
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0)
        {
            perror("Error creating new file");
            return;
        }
        close(fd);

        // ✅ Send ACK to client (Block 0)
        tftp_packet ack_packet;
        memset(&ack_packet, 0, sizeof(ack_packet));
        ack_packet.opcode = htons(ACK);
        ack_packet.body.ack_packet.block_number = htons(0); // ✅ Acknowledge WRQ

        if (sendto(sockfd, &ack_packet, sizeof(ack_packet), 0,
                   (struct sockaddr *)&client_addr, client_len) < 0)
        {
            perror("Failed to send ACK");
            return;
        }
        printf("ACK sent to client. File ready for writing.\n");

        // Start receiving the file with the appropriate mode
        receive_file(sockfd, client_addr, client_len, filename, MODE_NORMAL);
    }
    else if (ntohs(packet->opcode) == RRQ) // ✅ Handle GET (Read Request)
    {
        printf("Received read request for file: %s\n", filename);
        struct stat file_stat;
        if (stat(filename, &file_stat) != 0)
        {
            // File does not exist, send error packet
            tftp_packet error_packet;
            memset(&error_packet, 0, sizeof(error_packet));
            error_packet.opcode = htons(ERROR);
            error_packet.body.error_packet.error_code = htons(1); // File Not Found
            strcpy(error_packet.body.error_packet.error_msg, "File not found");

            sendto(sockfd, &error_packet, sizeof(error_packet), 0,
                   (struct sockaddr *)&client_addr, client_len);

            printf("File not found, sent error response.\n");
            return;
        }

        printf("File found. Sending file to client...\n");
        // ✅ Send ACK for RRQ (Block 0)
        tftp_packet ack_packet;
        memset(&ack_packet, 0, sizeof(ack_packet));
        ack_packet.opcode = htons(ACK);
        ack_packet.body.ack_packet.block_number = htons(0);

        sendto(sockfd, &ack_packet, sizeof(ack_packet), 0,
               (struct sockaddr *)&client_addr, client_len);

       // Start sending the file with the appropriate mode
       send_file(sockfd, client_addr, client_len, filename, MODE_NORMAL);
    }
}
