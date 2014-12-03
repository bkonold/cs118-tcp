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
#include <time.h>
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
save_file_and_exit(vector<Packet> packets, char* filename, int sockfd, struct sockaddr_in dest_addr) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        error("could not open file for writing");
    }

    int fileLength = 0;
    for (int i = 0; i < packets.size(); i++) {
        fileLength += packets[i].getDataLen();
    }

    char* buffer = (char*)malloc(fileLength);
    for (int i = 0; i < packets.size(); ++i) {
        for (int j = 0; j < packets[i].getDataLen(); ++j) {
            buffer[(i * DATA_LEN) + j] = packets[i].getData()[j];
        }
    }

    int bytesWritten = fwrite(buffer, sizeof(char), fileLength, file);
    if (bytesWritten != fileLength) {
        error("writing to file failed");
    }

    Packet ackPkt(-1, EOF_ACK, NULL, 0);
    printf("Sending ACK with ACK number: %d\n", ackPkt.getAckNum());
    int serializedLength;
    char* ackbuf = ackPkt.serialize(&serializedLength);
    int bytesSent = sendto(sockfd, (void*)ackbuf, serializedLength, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (bytesSent < 0) {
        error("ERROR on sending ack");
    }

    time_t t = time(0);
    char packetData[2048];
    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len;
        int packetDataLength = recvfrom(sockfd, packetData, 2048, 0, (struct sockaddr *) &cli_addr, &cli_len);
        // TODO potentially check that you got eof ack from proper server
        if (packetDataLength < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                error("ERROR on recvfrom");
            }
            continue;
        }
        else if (packetDataLength > 0) {
            Packet pkt(packetData, packetDataLength);
            if (pkt.isEOF_ACK()) {
                exit(0);
            }
        }
        if (time(0) - t > TIMEOUT) {
            Packet eofAckPkt(-1, EOF_ACK, NULL, 0);
            printf("Sending ACK with ACK number: %d\n", eofAckPkt.getAckNum());
            int eofAckSerLen;
            char* eofAckBuf = eofAckPkt.serialize(&eofAckSerLen);
            int bytesSent = sendto(sockfd, (void*)eofAckBuf, eofAckSerLen, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            t = time(0);
        }
    }
}

int main(int argc, char *argv[])
{
    srand(time(0));
    int sockfd, newsockfd, portno, pid;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
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
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = 48120;
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

    // server info
    char* filename = argv[3];
    int serverPort = atoi(argv[2]);
    char* serverName = argv[1];

    // resolve hostname using DNS
    hostent *server = gethostbyname(serverName);

    if (!server) {
        error("gethostbyname failed");
    }

    char* serverAddress = inet_ntoa( (struct in_addr) *((struct in_addr *) server->h_addr));
    Packet requestPacket(-1, REQUEST_PACKET, filename, strlen(filename));

    printf("IP for hostname %s: %s\n", serverName, serverAddress);

    // send request packet to server
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(serverPort);
    dest_addr.sin_addr.s_addr = inet_addr(serverAddress);

    int length;
    string msg(requestPacket.serialize(&length));
    printf("Message string: %s\n", msg.c_str());

    int bytesSent = sendto(sockfd, msg.c_str(), length+1, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (bytesSent < 0) {
        error("ERROR on sending request");
    }

    time_t t = time(0);  

    char packetData[2048];
    int packetDataLength;
    int expectedSeqNum = 0;
    vector<Packet> packets;

    while (1) {
        packetDataLength = recvfrom(sockfd, packetData, 2048, 0, (struct sockaddr *) &cli_addr, &clilen);
        if (packetDataLength < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                error("ERROR on recvfrom");
            }
            continue;
        }
        else if (packetDataLength > 0) {
            Packet pkt(packetData, packetDataLength);
            printf("Got data packet with SEQ number: %d\n", pkt.getSeqNum());
            if (pkt.getSeqNum() == (expectedSeqNum * DATA_LEN)) {
                packets.push_back(pkt);
                if (pkt.isEOF()) {
                    save_file_and_exit(packets, filename, sockfd, dest_addr);
                }
                expectedSeqNum += 1;
                Packet ackPkt(-1, expectedSeqNum * DATA_LEN, NULL, 0);
                printf("Sending ACK with ACK number: %d\n", ackPkt.getAckNum());
                int serializedLength;
                char* buffer = ackPkt.serialize(&serializedLength);
                bytesSent = sendto(sockfd, (void*)buffer, serializedLength, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

                if (bytesSent < 0) {
                    error("ERROR on sending ack");
                }
            }
            t = time(0);
            break;
        }
        if (time(0) - t > TIMEOUT) {
            printf("TIMEOUT on request\n");
            bytesSent = sendto(sockfd, msg.c_str(), length+1, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

            if (bytesSent < 0) {
                error("ERROR on sending request");
            }
            t = time(0);
        }
    }

    while (1) {
        packetDataLength = recvfrom(sockfd, packetData, 2048, 0, (struct sockaddr *) &cli_addr, &clilen);
        int r = (rand() % 10) + 1;
        if (r < 2) {
            continue;
        }

        if (packetDataLength < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                error("ERROR on recvfrom");
            }

            if (time(0) - t > TIMEOUT) {
                printf("TIMEOUT, haven't received expected data\n");
                Packet ackPkt(-1, expectedSeqNum * DATA_LEN, NULL, 0);
                int serializedLength;
                char* buffer = ackPkt.serialize(&serializedLength);
                printf("Sending ACK with ACK number: %d\n", ackPkt.getAckNum());
                bytesSent = sendto(sockfd, (void*)buffer, serializedLength, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

                if (bytesSent < 0) {
                    error("ERROR on sending ack");
                }
                t = time(0);
            }
            
            continue;
        }
        // TODO remember to check for DATA_PACKET packet type and ignore others
        else if (packetDataLength > 0) {
            Packet pkt(packetData, packetDataLength);
            if (pkt.getSeqNum() == (expectedSeqNum * DATA_LEN)) {
                printf("Got data packet with SEQ number: %d\n", pkt.getSeqNum());
                packets.push_back(pkt);
                if (pkt.isEOF()) {
                    save_file_and_exit(packets, filename, sockfd, dest_addr);
                }
                expectedSeqNum += 1;
                Packet ackPkt(-1, expectedSeqNum * DATA_LEN, NULL, 0);
                printf("Sending ACK with ACK number: %d\n", ackPkt.getAckNum());
                int serializedLength;
                char* buffer = ackPkt.serialize(&serializedLength);
                bytesSent = sendto(sockfd, (void*)buffer, serializedLength, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

                if (bytesSent < 0) {
                    error("ERROR on sending ack");
                }
            }
            t = time(0);
            continue;
        }
    }

    printf("hit end of main function\n");
    return 0; /* we never get here */
}