/* A simple server in the internet domain using TCP
The port number is passed as an argument 
This version runs forever, forking off a separate 
process for each connection
*/
#include <stdio.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <strings.h>
#include <sys/wait.h> /* for the waitpid() system call */
#include <signal.h> /* signal name macros, and the kill() prototype */
#include <unistd.h>
#include <time.h>
#include <iostream>
#include <sys/fcntl.h>
#include <errno.h>

#include "Packet.h"

using namespace std;

int eofPosition = -1;

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void error(char *msg)
{
    perror(msg);
    exit(1);
}

char*
file_to_str(FILE* file, int* len) {

    fseek(file, 0L, SEEK_END);
    *len = ftell(file);
    fseek(file, 0L, SEEK_SET);

    char* fileContents = (char*)malloc(*len+1);
    bzero(fileContents, *len+1);

    fread(fileContents, 1, *len, file); 

    return fileContents;
}

bool
send_nth_packet(int n, char* fileContents, int fileLength, int sockfd, struct sockaddr_in cli_addr, socklen_t clilen) {
    if ((n+1) * DATA_LEN > fileLength) {
        eofPosition = n;
        Packet pkt(n*DATA_LEN, EOF_PACKET, fileContents + n*DATA_LEN, fileLength - n*DATA_LEN);
        int serializedLength;
        char* buffer = pkt.serialize(&serializedLength);
        int err = sendto(sockfd, (void*)buffer, serializedLength, 0, (struct sockaddr *) &cli_addr, clilen);
        if (err < 0) {
            error("ERROR on send_nth_packet");
        }
        free(buffer);
        return false;
    }
    else {
        Packet pkt(n*DATA_LEN, DATA_PACKET, fileContents + n*DATA_LEN, DATA_LEN);
        int serializedLength;
        char* buffer = pkt.serialize(&serializedLength);
        int err = sendto(sockfd, (void*)buffer, serializedLength, 0, (struct sockaddr *) &cli_addr, clilen);
        if (err < 0) {
            error("ERROR on send_nth_packet");
        }
        free(buffer);
    }
    return true;
}

int main(int argc, char *argv[])
{
    srand(time(0));
    int sockfd, newsockfd, portno, pid;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    struct sigaction sa;          // for signal SIGCHLD

    if (argc < 2) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    if (sockfd < 0) {
        error("ERROR opening socket");
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        error("ERROR on binding");
    }

    clilen = sizeof(cli_addr);

    /****** Kill Zombie Processes ******/
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    /*********************************/
    char* fileContents;
    int fileLength;
    int windowStart = 0;
    int currentClientPort = -1;
    unsigned long currentClientIp = -1;
    time_t t;
    while (1) {
        char packetData[2048];
        int packetDataLength = recvfrom(sockfd, packetData, 2048, 0, (struct sockaddr *) &cli_addr, &clilen);

        int r = (rand() % 10) + 1;
        if (r < 2) {
            continue;
        }

        if (packetDataLength < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                error("ERROR on recvfrom");
            }
            if (currentClientPort != -1 && time(0) - t > TIMEOUT) {
                printf("TIMEOUT on ACK\n");
                int stop;
                if (eofPosition != -1)
                    stop = eofPosition + 1;
                else
                    stop = windowStart + WINDOW_SIZE;
                t = time(0);
                for (int i = windowStart; i < stop; i++) {
                    if (!send_nth_packet(i, fileContents, fileLength, sockfd, cli_addr, clilen))
                        break;
                }
           
            }
            continue;
        }

        Packet rcvdPacket(packetData, packetDataLength);
        printf("source port: %d\n", htons(cli_addr.sin_port));

        // potentially need to handle receiving a request packet in the middle of handling a request
        if (rcvdPacket.isRequest() && currentClientPort == -1) {
            string filePath(rcvdPacket.getData(), rcvdPacket.getDataLen());
            printf("File Path: %s\n", filePath.c_str());
            FILE* file = fopen(filePath.c_str(), "rb");
            if (!file) {
                // FILE NOT FOUND
                Packet responsePacket(0, NOT_FOUND_PACKET, NULL, 0);
                int serializedLength;
                char* buffer = responsePacket.serialize(&serializedLength);
                int err = sendto(sockfd, (void*)buffer, serializedLength, 0, (struct sockaddr *) &cli_addr, clilen);
                if (err < 0)
                    error("ERROR on FNF sendto");
                free(buffer);
                continue;
            }

            currentClientPort = htons(cli_addr.sin_port);
            currentClientIp = cli_addr.sin_addr.s_addr;

            fileContents = file_to_str(file, &fileLength);
            printf("File Length: %d\n", fileLength);
            t = time(0);
            for (int i = 0; i < WINDOW_SIZE; i++) {
                if (!send_nth_packet(i, fileContents, fileLength, sockfd, cli_addr, clilen))
                    break;
            }
        }
        else if (rcvdPacket.isEOF_ACK()) {
            if (htons(cli_addr.sin_port) == currentClientPort && cli_addr.sin_addr.s_addr == currentClientIp) {
                // reset variables
                free(fileContents);
                fileLength = 0;
                windowStart = 0;
                currentClientPort = -1;
                currentClientIp = -1;
                eofPosition = -1;
            }

            Packet pkt(-1, EOF_ACK, NULL, 0);
            int serializedLength;
            char* ackbuf = pkt.serialize(&serializedLength);
            int bytesSent = sendto(sockfd, (void*)ackbuf, serializedLength, 0, (struct sockaddr *)&cli_addr, clilen);

            if (bytesSent < 0) {
                error("ERROR on sending ack");
            }
        }
        else if (rcvdPacket.isACK()) {
            printf("Received ACK packet with ACK number %d\n", rcvdPacket.getAckNum());
            printf("Window Start: %d\n", windowStart);
            int ackNumDivided = rcvdPacket.getAckNum()/DATA_LEN;
            if (ackNumDivided > windowStart) {
                int stop;
                if (eofPosition != -1)
                    stop = eofPosition + 1;
                else
                    stop = windowStart + WINDOW_SIZE;
                t = time(0);
                for (int i = windowStart + WINDOW_SIZE; i < stop; i++) {
                    if (!send_nth_packet(i, fileContents, fileLength, sockfd, cli_addr, clilen))
                        break;
                }
                windowStart = ackNumDivided;
            }
        }

    } /* end of while */
    return 0; /* we never get here */
}