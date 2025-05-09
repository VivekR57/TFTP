/* Common file for server & client */

#ifndef TFTP_H
#define TFTP_H

#include <stdint.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define PORT 6969
#define BUFFER_SIZE 516 // TFTP data packet size (512 bytes data + 4 bytes header)
#define TIMEOUT_SEC 5   // Timeout in seconds
#define BLOCK_SIZE 512

// Modes
#define MODE_NORMAL   0
#define MODE_OCTET    1
#define MODE_NETASCII 2

// TFTP OpCodes
typedef enum
{
    RRQ = 1,  // Read Request
    WRQ = 2,  // Write Request
    DATA = 3, // Data Packet
    ACK = 4,  // Acknowledgment
    ERROR = 5 // Error Packet
} tftp_opcode;



// TFTP Packet Structure
typedef struct
{
    uint16_t opcode; // Operation code (RRQ/WRQ/DATA/ACK/ERROR)
    union
    {
        struct
        {
            char filename[256];
            char mode[8]; // Typically "octet"
        } request;        // RRQ and WRQ
        struct
        {
            uint16_t block_number;
            char data[512];
        } data_packet; // DATA
        struct
        {
            uint16_t block_number;
        } ack_packet; // ACK
        struct
        {
            uint16_t error_code;
            char error_msg[512];
        } error_packet; // ERROR
    } body;
} tftp_packet;

void send_file(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, char *filename, int mode);
void receive_file(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, char *filename, int mode);

#endif // TFTP_H
