#ifndef RDT_H
#define RDT_H

#include <string>
#include <sstream>

#define DELIM ','
#define DATA_PACKET (-1)
#define EOF_PACKET (-2)
#define EOF_ACK (-3)
#define REQUEST_PACKET (-4)
#define NOT_FOUND_PACKET (-5)
#define DATA_LEN 1000
#define INITIAL_SEQ_NUM 0
#define WINDOW_SIZE 10
#define TIMEOUT (1)

class Packet {
public:
    Packet(int seqNum, int ackNum, char* data, int dataLen) {
        m_seqNum = seqNum;
        m_ackNum = ackNum;
        m_data = data;
        m_dataLen = dataLen;
    }

    Packet(char* packetData, int len) {
        char* c1 = std::find(packetData, packetData + len, DELIM);
        char* c2 = std::find(c1+1, packetData + len, DELIM);

        std::string seqNumStr(packetData, c1 - packetData);
        std::string ackNumStr(c1 + 1, c2 - c1 - 1);

        int seqNum = atoi(seqNumStr.c_str());
        int ackNum = atoi(ackNumStr.c_str());

        m_seqNum = seqNum;
        m_ackNum = ackNum; 
        m_dataLen = len - (seqNumStr.length() + ackNumStr.length() + 2);     
        m_data = (char*)malloc(m_dataLen);

        for (char* i = c2 + 1; i < packetData + len; ++i) {
            m_data[i - (c2 + 1)] = (*i);
        }
    }

    // remember to free string after calling
    char* serialize(int* len) {
        std::string headerInfo;
        headerInfo += to_string(m_seqNum);
        headerInfo += ',';
        headerInfo += to_string(m_ackNum);
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
    bool isData() { return m_ackNum == DATA_PACKET; }
    bool isEOF_ACK() { return m_ackNum == EOF_ACK; }

    void setSeqNum(int seqNum) { m_seqNum = seqNum; }
    void setAckNum(int ackNum) { m_ackNum = ackNum; }

    int getSeqNum() { return m_seqNum; }
    int getAckNum() { return m_ackNum; }
    char* getData() { return m_data; }
    int getDataLen() { return m_dataLen; }

private:
    int m_seqNum;
    int m_ackNum;
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
};

#endif