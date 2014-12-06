/* A simple server in the internet domain using TCP
The port number is passed as an argument 
This version runs forever, forking off a separate 
process for each connection
*/
#include <stdio.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <netdb.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <strings.h>
#include <sys/wait.h> /* for the waitpid() system call */
#include <signal.h> /* signal name macros, and the kill() prototype */
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/fcntl.h>
#include <vector>
#include <errno.h>

#include "Packet.h"

using namespace std;

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void error(char *msg)
{
    perror(msg);
    exit(1);
}

void
send_packet(Packet pkt, int sockfd, struct sockaddr_in destAddr) {
    if (pkt.isRequest()) {
        string request(pkt.getData(), pkt.getDataLen());
        printf("Sending REQUEST with filename: %s\n", request.c_str());
    }
    else if (pkt.isACK()) {
        printf("Sending ACK with ACKNUM: %d\n", pkt.getAckNum());
    }
    else if (pkt.isEOF_ACK()) {
        printf("Sending EOF ACK\n");
    }

    int serializedLength;
    char* buffer = pkt.serialize(&serializedLength);
    int bytesSent = sendto(sockfd, (void*)buffer, serializedLength, 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
    free(buffer);

    if (bytesSent < 0) {
        error("ERROR on sending packet");
    }
}

void
save_file_and_exit(vector<Packet> packets, char* filename, int sockfd, struct sockaddr_in destAddr) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        error("ERROR: could not open file for writing");
    }

    int fileLength = 0;
    for (int i = 0; i < packets.size(); i++) {
        fileLength += packets[i].getDataLen();
    }

    char* buffer = (char*)malloc(fileLength);
    int totalSize = 0;
    for (int i = 0; i < packets.size(); ++i) {
        for (int j = 0; j < packets[i].getDataLen(); ++j) {
            buffer[totalSize + j] = packets[i].getData()[j];
        }
        totalSize += packets[i].getDataLen();
    }

    int bytesWritten = fwrite(buffer, sizeof(char), fileLength, file);
    if (bytesWritten != fileLength) {
        error("ERROR: writing to file failed");
    }

    free(buffer);
    fclose(file);

    Packet ackPkt(-1, EOF_ACK, NULL, 0);
    send_packet(ackPkt, sockfd, destAddr);

    struct timeval timeVal;
    gettimeofday(&timeVal, NULL);
    long t = timeVal.tv_usec/1000 + timeVal.tv_sec*1000;
    char packetData[2048];
    while (1) {
        struct sockaddr_in servAddr;
        socklen_t servLen = sizeof(servAddr);
        int packetDataLength = recvfrom(sockfd, packetData, 2048, 0, (struct sockaddr *) &servAddr, &servLen);

        int r = (rand() % 100) + 1;
        if (r <= PROBABILITY_PACKET_LOST * 100) {
            continue;
        }
        r = (rand() % 100) + 1;
        if (r <= PROBABILITY_PACKET_CORRUPT * 100) {
            continue;
        }

        if (packetDataLength < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                error("ERROR on recvfrom");
            }
            gettimeofday(&timeVal, NULL);
            long now = timeVal.tv_usec/1000 + timeVal.tv_sec*1000;
            // check for timeout, otherwise just go back and try recvfrom again
            if (now - t > TIMEOUT) {
                Packet eofAckPkt(-1, EOF_ACK, NULL, 0);
                printf("TIMEOUT waiting for EOF ACK. RETRANSMISSION: ");
                send_packet(eofAckPkt, sockfd, destAddr);
                t = now;
            }
        }
        else if (packetDataLength > 0) {
            if (servAddr.sin_port == destAddr.sin_port && servAddr.sin_addr.s_addr == destAddr.sin_addr.s_addr) {
                Packet pkt(packetData, packetDataLength);
                if (!pkt.isCorrupt() && pkt.isEOF_ACK()) {
                    exit(0);
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    srand(time(0));
    int sockfd, portno;
    struct sockaddr_in recvAddr, servAddr;
    socklen_t servLen = sizeof(servAddr);
    struct sigaction sa;          // for signal SIGCHLD

    if (argc < 4) {
        fprintf(stderr,"ERROR, usage incorrect\n");
        exit(1);
    }
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        error("ERROR opening socket");
    }
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    bzero((char *) &recvAddr, sizeof(recvAddr));
    portno = 48120;
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_addr.s_addr = INADDR_ANY;
    recvAddr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) &recvAddr, sizeof(recvAddr)) < 0) {
        error("ERROR on binding");
    }

    /****** Kill Zombie Processes ******/
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    /*********************************/

    // server info
    char* filename = argv[3];
    int serverPort = atoi(argv[2]);
    char* serverName = argv[1];

    // resolve hostname using DNS
    hostent *server = gethostbyname(serverName);

    if (!server) {
        error("gethostbyname failed");
    }

    char* serverIpAddress = inet_ntoa( (struct in_addr) *((struct in_addr *) server->h_addr));
    Packet requestPacket(-1, REQUEST_PACKET, filename, strlen(filename)+1);

    printf("IP for hostname %s: %s\n", serverName, serverIpAddress);

    // fill in server (sender) details
    struct sockaddr_in destAddr;
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(serverPort);
    destAddr.sin_addr.s_addr = inet_addr(serverIpAddress);

    send_packet(requestPacket, sockfd, destAddr);

    struct timeval timeVal;
    gettimeofday(&timeVal, NULL);
    unsigned long t = timeVal.tv_usec/1000 + timeVal.tv_sec*1000;

    char packetData[2048];
    int packetDataLength;
    int expectedSeqNum = 0;
    vector<Packet> packets;

    while (1) {
        packetDataLength = recvfrom(sockfd, packetData, 2048, 0, (struct sockaddr *) &servAddr, &servLen);


        if (packetDataLength < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                error("ERROR on recvfrom");
            }
            gettimeofday(&timeVal, NULL);
            long now = timeVal.tv_usec/1000 + timeVal.tv_sec*1000;
            if (now - t > TIMEOUT) {
                printf("TIMEOUT waiting for data\n");
                if (expectedSeqNum == 0) {
                    printf("RETRANSMISSION: ");
                    send_packet(requestPacket, sockfd, destAddr);
                }
                else {
                    Packet ackPkt(-1, expectedSeqNum, NULL, 0);
                    printf("RETRANSMISSION: ");
                    send_packet(ackPkt, sockfd, destAddr);
                }
                t = now;
            }
        }
        else if (packetDataLength > 0) {
            if (servAddr.sin_port == destAddr.sin_port && servAddr.sin_addr.s_addr == destAddr.sin_addr.s_addr) {
                Packet pkt(packetData, packetDataLength);
                // simulate corruption and packet loss
                int r = (rand() % 100) + 1;
                if (r <= PROBABILITY_PACKET_LOST * 100) {
                    continue;
                }
                r = (rand() % 100) + 1;
                if (r <= PROBABILITY_PACKET_CORRUPT * 100) {
                    pkt.setSeqNum(pkt.getSeqNum() + 1);
                }
                if (pkt.isData()) {
                    if (pkt.getSeqNum() == expectedSeqNum && !pkt.isCorrupt()) {
                        printf("Got DATA packet with SEQ number: %d\n", pkt.getSeqNum());
                        packets.push_back(pkt);
                        if (pkt.isEOF()) {
                            save_file_and_exit(packets, filename, sockfd, destAddr);
                        }
                        expectedSeqNum += pkt.getDataLen();
                        Packet ackPkt(-1, expectedSeqNum, NULL, 0);
                        gettimeofday(&timeVal, NULL);
                        t = timeVal.tv_usec/1000 + timeVal.tv_sec*1000;
                        send_packet(ackPkt, sockfd, destAddr);
                    }
                    else {
                        Packet ackPkt(-1, expectedSeqNum, NULL, 0);
                        printf("Got out of order packet. Resending ACK with ACKNUM %d\n", ackPkt.getAckNum());
                        send_packet(ackPkt, sockfd, destAddr);
                    }
                }
            }
        }
    }
    return 0; /* we never get here */
}