#ifndef RDT_H
#define RDT_H

#include <string.h>
#include <sstream>

#define DELIM ','
#define DATA_PACKET (-1)
#define EOF_PACKET (-2)
#define EOF_ACK (-3)
#define REQUEST_PACKET (-4)
#define NOT_FOUND_PACKET (-5)
#define DATA_LEN 1000
#define INITIAL_SEQ_NUM 0
#define WINDOW_SIZE 1453
#define TIMEOUT (175)

#define PROBABILITY_PACKET_LOST (0.15)
#define PROBABILITY_PACKET_CORRUPT (0.15)

class Packet {
public:
    ~Packet() {
        free(m_data);
    }

    Packet(int seqNum, int ackNum, char* data, int dataLen) {
        m_seqNum = seqNum;
        m_ackNum = ackNum;
        m_dataLen = dataLen;
        m_data = (char*)malloc(dataLen);
        for (int i = 0; i < dataLen; ++i) {
            m_data[i] = data[i];
        }
        m_checksum = hash();
    }

    Packet(const Packet &pkt) {
        m_seqNum = pkt.m_seqNum;
        m_ackNum = pkt.m_ackNum;
        m_dataLen = pkt.m_dataLen;
        m_data = (char*)malloc(pkt.m_dataLen);
        for (int i = 0; i < pkt.m_dataLen; ++i) {
            m_data[i] = pkt.m_data[i];
        }
        m_checksum = hash();
    }

    Packet(char* packetData, int len) {
        char* c1 = strchr(packetData, DELIM);
        char* c2 = strchr(c1+1, DELIM);
        char* c3 = strchr(c2+1, DELIM);

        std::string seqNumStr(packetData, c1 - packetData);
        std::string ackNumStr(c1 + 1, (c2 - c1) - 1);
        std::string checksumStr(c2 + 1, (c3 - c2) - 1);

        int seqNum = atoi(seqNumStr.c_str());
        int ackNum = atoi(ackNumStr.c_str());
        int checksum = atoi(checksumStr.c_str());

        m_seqNum = seqNum;
        m_ackNum = ackNum; 
        m_checksum = checksum;
        m_dataLen = len - (seqNumStr.length() + ackNumStr.length() + checksumStr.length() + 3);     
        m_data = (char*)malloc(m_dataLen);

        for (char* i = c3 + 1; i < packetData + len; ++i) {
            m_data[i - (c3 + 1)] = (*i);
        }
    }

    // remember to free string after calling
    char* serialize(int* len) {
        std::string headerInfo;
        headerInfo += to_string(m_seqNum);
        headerInfo += ',';
        headerInfo += to_string(m_ackNum);
        headerInfo += ',';
        headerInfo += to_string(m_checksum);
        headerInfo += ',';

        *len = headerInfo.length() + m_dataLen;
        char* str = (char*)malloc(*len);

        for (int i = 0; i < headerInfo.length(); ++i) {
            str[i] = headerInfo[i];
        }
        for (int i = 0; i < m_dataLen; ++i) {
            str[i + headerInfo.length()] = m_data[i];
        }

        return str;
    }

    bool isEOF() { return m_ackNum == EOF_PACKET; }
    bool isACK() { return m_ackNum >= 0; }
    bool isRequest() { return m_ackNum == REQUEST_PACKET; }
    bool isNotFound() { return m_ackNum == NOT_FOUND_PACKET; }
    bool isData() { return (m_ackNum == DATA_PACKET) || (m_ackNum == EOF_PACKET); }
    bool isEOF_ACK() { return m_ackNum == EOF_ACK; }
    bool isCorrupt() { return hash() != m_checksum; }

    void setSeqNum(int seqNum) { m_seqNum = seqNum; }
    void setAckNum(int ackNum) { m_ackNum = ackNum; }

    int getSeqNum() { return m_seqNum; }
    int getAckNum() { return m_ackNum; }
    char* getData() { return m_data; }
    int getDataLen() { return m_dataLen; }

private:
    int m_seqNum;
    int m_ackNum;
    int m_checksum;
    char* m_data;
    int m_dataLen;

    // because ubuntu 12.04 doesn't work with to_string
    template <typename T>
    std::string to_string(T value)
    {
        std::ostringstream os ;
        os << value ;
        return os.str() ;
    }

    std::size_t hash() 
    {
        int size = m_dataLen + m_dataLen % (sizeof (int));
        char* paddedData = (char*)malloc(size);
        for (int i = 0; i < m_dataLen; ++i) {
            paddedData[i] = m_data[i];
        }
        for (int i = 0; i < m_dataLen % (sizeof (int)); ++i) {
            paddedData[i + m_dataLen] = 0;
        }

        int hashValue = m_seqNum ^ (m_ackNum<<1);

        for (int i = 0; i < size; i += sizeof(int)) {
            hashValue = (hashValue >> 1) ^ ((((int)paddedData[i])) << 1);
        }
        free(paddedData);
        return hashValue;
    }
};

#endif