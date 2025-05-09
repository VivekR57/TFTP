#ifndef TFTP_CLIENT_H
#define TFTP_CLIENT_H

typedef struct 
{
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t server_len;
    char server_ip[INET_ADDRSTRLEN];
    int mode; // 0: Normal, 1: Octet, 2: NetASCII
} tftp_client_t;

// Function prototypes
void connect_to_server(tftp_client_t *client, char *ip, int port);
void put_file(tftp_client_t *client, char *filename,int fd);
void get_file(tftp_client_t *client, char *filename);
void disconnect(tftp_client_t *client);
void process_command(tftp_client_t *client, char *command,int*is_connect) ;


void send_request(int sockfd, struct sockaddr_in server_addr, char *filename, int opcode);
void receive_request(int sockfd, struct sockaddr_in server_addr, char *filename, int opcode);

#endif