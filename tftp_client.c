#include "tftp.h"
#include "tftp_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

int main()
{
    char command[256];
    tftp_client_t client;
    memset(&client, 0, sizeof(client)); // Initialize client structure
    int is_connect = 0;                 // Flag to check connection status

    // Main loop for command-line interface
    while (1)
    {
        printf("Options:\n1. connect\n2. put\n3. get\n4. mode\n5. exit\ntftp> ");
        fgets(command, sizeof(command), stdin);
        // Remove newline character
        command[strcspn(command, "\n")] = 0;

        // Process the command
        process_command(&client, command, &is_connect);
    }

    return 0;
}

// Function to process commands
void process_command(tftp_client_t *client, char *command, int *is_connect)
{
    if (strcmp(command, "connect") == 0)
    {
        char ip[INET_ADDRSTRLEN];
        printf("Enter the Server IP: ");
        fgets(ip, sizeof(ip), stdin);
        ip[strcspn(ip, "\n")] = 0;

        connect_to_server(client, ip, PORT);
        *is_connect = 1;
    }
    else if (strcmp(command, "put") == 0)
    {
        if (!is_connect)
        {
            printf("Error: Please connect to a server first using 'connect' command.\n");
        }
        else
        {
            char filename[256];
            printf("Enter filename to upload: ");
            fgets(filename, sizeof(filename), stdin);
            filename[strcspn(filename, "\n")] = 0;
            int fd = open(filename, O_RDONLY);
            if (fd == -1)
            {
                perror("Error: Cannot open file");
                return;
            }
            put_file(client, filename, fd);
            close(fd);
        }
    }
    else if (strcmp(command, "get") == 0)
    {
        if (!is_connect)
        {
            printf("Error: Please connect to a server first using 'connect' command.\n");
        }
        else
        {
            char filename[256];
            printf("Enter filename to download: ");
            fgets(filename, sizeof(filename), stdin);
            filename[strcspn(filename, "\n")] = 0;
            get_file(client, filename);
        }
    }
    else if (strcmp(command, "mode") == 0)
    {
        printf("Select mode:\n1. Normal\n2. Octet\n3. NetASCII\n> ");
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "1") == 0)
        {
            client->mode = MODE_NORMAL;
            printf("Mode set to Normal.\n");
        }
        else if (strcmp(command, "2") == 0)
        {
            client->mode = MODE_OCTET;
            printf("Mode set to Octet.\n");
        }
        else if (strcmp(command, "3") == 0)
        {
            client->mode = MODE_NETASCII;
            printf("Mode set to NetASCII.\n");
        }
        else
        {
            printf("Invalid mode selection.\n");
        }
    }
    else if (strcmp(command, "exit") == 0)
    {
        disconnect(client);
        exit(0);
    }
    else
    {
        printf("Invalid command.\n");
    }
}

// This function is to initialize socket with given server IP, no packets sent to server in this function
void connect_to_server(tftp_client_t *client, char *ip, int port)
{
    client->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client->sockfd < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&client->server_addr, 0, sizeof(client->server_addr));
    client->server_addr.sin_family = AF_INET;
    client->server_addr.sin_port = htons(port);
    client->server_addr.sin_addr.s_addr = inet_addr(ip);

    client->server_len = sizeof(client->server_addr);
    strncpy(client->server_ip, ip, INET_ADDRSTRLEN);

    printf("Connected to server %s on port %d\n", ip, port);
}

void put_file(tftp_client_t *client, char *filename, int fd)
{
    tftp_packet packet, ack_packet;
    int bytes_read;
    uint16_t block_number = 1;

    memset(&packet, 0, sizeof(packet));

    // Send WRQ request to server
    packet.opcode = htons(WRQ);
    strncpy(packet.body.request.filename, filename, sizeof(packet.body.request.filename) - 1);
    strcpy(packet.body.request.mode, client->mode == MODE_NORMAL ? "netascii" : (client->mode == MODE_OCTET ? "octet" : "netascii"));

    int c_size = sendto(client->sockfd, &packet, sizeof(packet), 0,
                        (struct sockaddr *)&client->server_addr, client->server_len);
    if (c_size < 0)
    {
        perror("Failed to send WRQ");
        return;
    }
    printf("Write request sent to server for file: %s\n", filename);

    // Wait for ACK from server
    int n = recvfrom(client->sockfd, &ack_packet, sizeof(ack_packet), 0, NULL, NULL);
    if (n < 0 || ntohs(ack_packet.opcode) != ACK)
    {
        perror("Failed to receive ACK from server");
        return;
    }

    printf("Server ACK received. Ready to send file data.\n");

    // Start sending file data based on mode
    while ((bytes_read = read(fd, packet.body.data_packet.data, BLOCK_SIZE)) > 0) // Always read 512 bytes
    {
        packet.opcode = htons(DATA);
        packet.body.data_packet.block_number = htons(block_number);

        // Handle NetASCII Mode: Convert \n to \r\n during sending
        if (client->mode == MODE_NETASCII)
        {
            char buffer[BLOCK_SIZE * 2]; // Buffer to hold converted data
            int buffer_index = 0;

            for (int i = 0; i < bytes_read; i++)
            {
                if (packet.body.data_packet.data[i] == '\n')
                {
                    buffer[buffer_index++] = '\r'; // Add \r before \n
                    buffer[buffer_index++] = '\n';
                }
                else
                {
                    buffer[buffer_index++] = packet.body.data_packet.data[i];
                }
            }

            // Copy the converted data back to the packet
            memcpy(packet.body.data_packet.data, buffer, buffer_index);
            bytes_read = buffer_index; // Update the number of bytes to send
        }

        // Send the data packet
        int sent_bytes = sendto(client->sockfd, &packet, 4 + bytes_read, 0,
                                (struct sockaddr *)&client->server_addr, client->server_len);
        if (sent_bytes < 0)
        {
            perror("Failed to send data packet");
            return;
        }

        // Wait for ACK from server
        n = recvfrom(client->sockfd, &ack_packet, sizeof(ack_packet), 0, NULL, NULL);
        if (n < 0 || ntohs(ack_packet.opcode) != ACK || ntohs(ack_packet.body.ack_packet.block_number) != block_number)
        {
            perror("Failed to receive ACK for data packet");
            return;
        }

        printf("Block %d sent and acknowledged.\n", block_number);
        block_number++;
    }

    if (bytes_read < 0)
    {
        perror("Error reading file");
    }
    else
    {
        printf("File transfer completed successfully.\n");
    }
}

void get_file(tftp_client_t *client, char *filename)
{
    tftp_packet packet, ack_packet;
    int fd;
    uint16_t expected_block = 1;
    memset(&packet, 0, sizeof(packet));

    // Send RRQ (Read Request) to server
    packet.opcode = htons(RRQ);
    strncpy(packet.body.request.filename, filename, sizeof(packet.body.request.filename) - 1);
    strcpy(packet.body.request.mode, client->mode == MODE_NORMAL ? "netascii" : (client->mode == MODE_OCTET ? "octet" : "netascii"));

    int c_size = sendto(client->sockfd, &packet, sizeof(packet), 0,
                        (struct sockaddr *)&client->server_addr, client->server_len);
    if (c_size < 0)
    {
        perror("Failed to send RRQ");
        return;
    }
    printf("Read request sent to server for file: %s\n", filename);

    // Wait for server ACK or error
    int n = recvfrom(client->sockfd, &ack_packet, sizeof(ack_packet), 0, NULL, NULL);
    if (n < 0)
    {
        perror("Failed to receive ACK from server");
        return;
    }

    if (ntohs(ack_packet.opcode) == ERROR)
    {
        printf("Server error: File not found on server.\n");
        return;
    }

    printf("Server ACK received. Preparing to receive file.\n");

    // Check if file already exists
    char new_filename[300];
    if (access(filename, F_OK) == 0)
    {
        snprintf(new_filename, sizeof(new_filename), "new_%s", filename);
        printf("Existing file found. Creating new file: %s\n", new_filename);
    }
    else
    {
        strncpy(new_filename, filename, sizeof(new_filename));
    }

    // Create new file for writing
    fd = open(new_filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
    {
        perror("Error creating file");
        return;
    }

    while (1)
    {
        // Receive DATA packet
        n = recvfrom(client->sockfd, &packet, sizeof(packet), 0, NULL, NULL);
        if (n < 0)
        {
            perror("Receive failed");
            continue; // Wait for retransmission
        }

        // Validate block number
        if (ntohs(packet.opcode) == DATA && ntohs(packet.body.data_packet.block_number) == expected_block)
        {
            // Handle NetASCII Mode: Convert \n to \r\n
            if (client->mode == MODE_NETASCII)
            {
                char buffer[BLOCK_SIZE * 2]; // Buffer to hold converted data
                int buffer_index = 0;

                for (int i = 0; i < n - 4; i++)
                {
                    if (packet.body.data_packet.data[i] == '\n')
                    {
                        buffer[buffer_index++] = '\r'; // Add \r before \n
                        buffer[buffer_index++] = '\n';
                    }
                    else
                    {
                        buffer[buffer_index++] = packet.body.data_packet.data[i];
                    }
                }

                // Write converted data to file
                int bytes_written = write(fd, buffer, buffer_index);
                if (bytes_written < 0)
                {
                    perror("Error writing to file");
                    close(fd);
                    return;
                }

                printf("Received Block %d, writing %d bytes to file (NetASCII conversion applied)\n", expected_block, bytes_written);
            }
            else
            {
                // Write received data to file (no conversion for Normal or Octet Mode)
                int bytes_written = write(fd, packet.body.data_packet.data, n - 4);
                if (bytes_written < 0)
                {
                    perror("Error writing to file");
                    close(fd);
                    return;
                }

                printf("Received Block %d, writing %d bytes to file\n", expected_block, bytes_written);
            }

            // Send ACK for received block
            memset(&ack_packet, 0, sizeof(ack_packet));
            ack_packet.opcode = htons(ACK);
            ack_packet.body.ack_packet.block_number = htons(expected_block);

            sendto(client->sockfd, &ack_packet, sizeof(ack_packet), 0,
                   (struct sockaddr *)&client->server_addr, client->server_len);

            expected_block++;

            // Stop when last block is < 512 bytes
            if (n - 4 < BLOCK_SIZE)
            {
                printf("File transfer complete! Saved as: %s\n", new_filename);
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

            sendto(client->sockfd, &ack_packet, sizeof(ack_packet), 0,
                   (struct sockaddr *)&client->server_addr, client->server_len);
        }
    }

    close(fd);
    printf("File received successfully!\n");
}
void disconnect(tftp_client_t *client)
{
    close(client->sockfd);
    printf("Disconnected from server.\n");
}