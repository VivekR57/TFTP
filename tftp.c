/* Common file for server & client */
#include "tftp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

void send_file(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, char *filename, int mode)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        perror("Error opening file");
        return;
    }

    tftp_packet packet, ack_packet;
    int bytes_read;
    uint16_t block_number = 1;

    while (1)
    {
        memset(&packet, 0, sizeof(packet));
        packet.opcode = htons(DATA);
        packet.body.data_packet.block_number = htons(block_number);

        // Read data from the file
        bytes_read = read(fd, packet.body.data_packet.data, BLOCK_SIZE); // Always read 512 bytes

        if (bytes_read < 0)
        {
            perror("Error reading file");
            close(fd);
            return;
        }

        // Handle NetASCII Mode: Convert \r\n to \n during sending
        if (mode == MODE_NETASCII)
        {
            char buffer[BLOCK_SIZE * 2]; // Buffer to hold converted data
            int buffer_index = 0;

            for (int i = 0; i < bytes_read; i++)
            {
                if (packet.body.data_packet.data[i] == '\r' && i + 1 < bytes_read && packet.body.data_packet.data[i + 1] == '\n')
                {
                    buffer[buffer_index++] = '\n'; // Convert \r\n to \n
                    i++;                           // Skip the next character
                }
                else
                {
                    buffer[buffer_index++] = packet.body.data_packet.data[i];
                }
            }

            // Send the converted data
            int sent_bytes = sendto(sockfd, &packet, 4 + buffer_index, 0,
                                    (struct sockaddr *)&client_addr, client_len);
        }
        else
        {
            // Send the data packet as-is for Normal or Octet Mode
            int sent_bytes = sendto(sockfd, &packet, 4 + bytes_read, 0,
                                    (struct sockaddr *)&client_addr, client_len);
        }

        // Wait for ACK from client
        int n = recvfrom(sockfd, &ack_packet, sizeof(ack_packet), 0, NULL, NULL);
        if (n < 0 || ntohs(ack_packet.opcode) != ACK || ntohs(ack_packet.body.ack_packet.block_number) != block_number)
        {
            perror("ACK not received for block");
            close(fd);
            return;
        }

        printf("Block %d sent and acknowledged.\n", block_number);
        block_number++;

        // Stop sending when the last block is < 512 bytes
        if (bytes_read < BLOCK_SIZE)
        {
            printf("Last block sent. Transfer complete!\n");
            break;
        }
    }

    printf("File transfer complete!\n");
    close(fd);
}

void receive_file(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, char *filename, int mode)
{
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        perror("Error opening file for writing");
        return;
    }

    tftp_packet packet, ack_packet;
    ssize_t bytes_written;
    uint16_t expected_block = 1;

    while (1)
    {
        // Receive DATA packet
        int n = recvfrom(sockfd, &packet, sizeof(packet), 0,
                         (struct sockaddr *)&client_addr, &client_len);
        if (n < 0)
        {
            perror("Receive failed, requesting retransmission");
            continue; // Wait for retransmission
        }

        // Validate if it's a DATA packet with the expected block number
        if (ntohs(packet.opcode) == DATA && ntohs(packet.body.data_packet.block_number) == expected_block)
        {
            // Write received data to file (no conversion for NetASCII Mode on server side)
            bytes_written = write(fd, packet.body.data_packet.data, n - 4);
            if (bytes_written < 0)
            {
                perror("Error writing to file");
                close(fd);
                return;
            }

            printf("Received Block %d, writing %ld bytes to file\n", expected_block, bytes_written);

            // Send ACK for received block
            memset(&ack_packet, 0, sizeof(ack_packet));
            ack_packet.opcode = htons(ACK);
            ack_packet.body.ack_packet.block_number = htons(expected_block);

            if (sendto(sockfd, &ack_packet, sizeof(ack_packet), 0,
                       (struct sockaddr *)&client_addr, client_len) < 0)
            {
                perror("Failed to send ACK");
            }
            else
            {
                printf("ACK sent for block %d\n", expected_block);
            }

            expected_block++;

            // Stop when last block is < 512 bytes
            if (n - 4 < BLOCK_SIZE)
            {
                printf("File transfer complete!\n");
                close(fd);
                break;
            }
        }
        else
        {
            // Resend last ACK to request retransmission
            printf("Unexpected block received. Expected: %d, Received: %d. Requesting retransmission.\n",
                   expected_block, ntohs(packet.body.data_packet.block_number));

            memset(&ack_packet, 0, sizeof(ack_packet));
            ack_packet.opcode = htons(ACK);
            ack_packet.body.ack_packet.block_number = htons(expected_block - 1);

            sendto(sockfd, &ack_packet, sizeof(ack_packet), 0,
                   (struct sockaddr *)&client_addr, client_len);
        }
    }

    printf("Server is idle and waiting for new connections...\n");
}