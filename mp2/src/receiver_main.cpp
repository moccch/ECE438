#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#define BUFSIZE 1024

#include <cstdint>
#include <deque>
#include <utility>
#include <string>
#include <unordered_map>
#include <iostream>

using namespace std;

#define MSS     ((unsigned long long)1460)
#define TCP_HEADER_SIZE ((unsigned long long)12)
#define TCP_PACKET_SIZE (MSS + TCP_HEADER_SIZE)
#define DEFAULT_TIMEOUT_INTERVAL   300  // Unit: ms
#define RECV_RWND (30*MSS)

#define FIN_FLAG 1

void printbuf(char* buf, int size){
    for(int i=0; i<size; i++){
        cout<<buf[i];
    }
    cout<<endl;
}

enum ConnectionState{
    ESTABLISHED,
    CLOSE_WAIT
};

class TCPSendPacket{
public:
    uint32_t seqNum;
    uint32_t datalength;
    uint32_t flags;
    uint8_t  data[MSS];
    TCPSendPacket():seqNum(0), datalength(0),flags(0){}
};

class TCPAckPacket{
public:
    uint32_t ackNum;
    uint32_t rwnd;
};

class TCPreceiver{
public:
    // Lock & Cond
    pthread_mutex_t mutexLock;
    pthread_cond_t isRecvAvailable;
    pthread_cond_t isUDPavailable;
    // Socket
    int sockfd;
    struct sockaddr senderaddr;
    bool isSetSender;
    // Reliable Data Transfer
    // deque<TCPSendPacket*> RecvQueue;
    unsigned char* tcpbuf;
    unsigned long long bufsize;
    unordered_map<unsigned long long, unsigned long long> unorderedPacket;
    unsigned long long LastByteRead;
    unsigned long long NextByteExpected;
    unsigned long long rwnd;
    // Connection
    ConnectionState connectionState;
public:
    TCPreceiver();
    int SetSocket(int socketfd);
    int SetSender(struct sockaddr* addr);
    int UdpRecv(TCPSendPacket* packet);
    int RecvDaemon();
    int appRecv(unsigned char* buf, unsigned long long size);
    int CheckFinished();
};

TCPreceiver::TCPreceiver(){
    mutexLock = PTHREAD_MUTEX_INITIALIZER;
    isRecvAvailable = PTHREAD_COND_INITIALIZER;
    isUDPavailable = PTHREAD_COND_INITIALIZER;
    LastByteRead = 0;
    NextByteExpected = 0;
    rwnd = RECV_RWND;
    bufsize = RECV_RWND;
    tcpbuf = new unsigned char[RECV_RWND];
    isSetSender = false;
    // Connection
    connectionState = ESTABLISHED;
}

int TCPreceiver::SetSocket(int socketfd){
    // Connection Info
    sockfd = socketfd;
    return 0;
}

int TCPreceiver::SetSender(struct sockaddr* addr){
    senderaddr = *addr;
    return 0;
}

int TCPreceiver::UdpRecv(TCPSendPacket* packet){
    pthread_mutex_lock(&mutexLock);
    if(rwnd-packet->datalength<0){
        pthread_mutex_unlock(&mutexLock);
        return -1;
    }
    if(packet->flags == FIN_FLAG){
        connectionState = CLOSE_WAIT;
        cout<<"FIN"<<endl;
        pthread_mutex_unlock(&mutexLock);
        return 0;
    }
    
    if(packet->seqNum==NextByteExpected){
        TCPAckPacket ackpkg;
        unsigned long long acknum = packet->seqNum+packet->datalength;
        while(unorderedPacket.find(acknum)!=unorderedPacket.end()){
            unsigned long long oldack = acknum;
            acknum = unorderedPacket[acknum];
            unorderedPacket.erase(oldack);
        }
        ackpkg.ackNum = acknum;
        // RWND
        // if(rwnd<(acknum-NextByteExpected)){
        //     return -1;
        // }
        rwnd -= (acknum - NextByteExpected);
        // cout<<"rwnd: "<<rwnd<<endl;
        ackpkg.rwnd = rwnd;
        sendto(sockfd, &ackpkg, sizeof(ackpkg), 0, (struct sockaddr*)&senderaddr, sizeof(senderaddr));
        for(unsigned long long i=0; i<packet->datalength; i++){
            tcpbuf[(packet->seqNum + i) % bufsize] = packet->data[i];
        }
        // cout<<"acknum"<<acknum<<endl;
        NextByteExpected = acknum;
    }else if(packet->seqNum > NextByteExpected){
        cout<<"Unordered Pkg:"<<packet->seqNum<<" NextByteExp:"<<NextByteExpected<<endl;
        TCPAckPacket ackpkg;
        ackpkg.ackNum = NextByteExpected;
        ackpkg.rwnd = rwnd;
        sendto(sockfd, &ackpkg, sizeof(ackpkg), 0, (struct sockaddr*)&senderaddr, sizeof(senderaddr));
        for(unsigned long long i=0; i<packet->datalength; i++){
            tcpbuf[(packet->seqNum + i) % bufsize] = packet->data[i];
        }
        unorderedPacket.insert(make_pair(packet->seqNum, packet->seqNum+packet->datalength));
    }else{
        TCPAckPacket ackpkg;
        ackpkg.ackNum = NextByteExpected;
        ackpkg.rwnd = rwnd;
        sendto(sockfd, &ackpkg, sizeof(ackpkg), 0, (struct sockaddr*)&senderaddr, sizeof(senderaddr));
    }
    pthread_mutex_unlock(&mutexLock);
    return 0;
}

int TCPreceiver::RecvDaemon(){
    TCPSendPacket packet;
    struct sockaddr_in peeraddr;
    socklen_t addrlen = sizeof(peeraddr);
    while(1){
        if(recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&peeraddr, &addrlen)>0){
            if(!isSetSender){
                pthread_mutex_lock(&mutexLock);
                SetSender((struct sockaddr*)&peeraddr);
                pthread_mutex_unlock(&mutexLock);
            }
            UdpRecv(&packet);
            pthread_cond_signal(&isRecvAvailable);
            // cout<<"Recv Seq:"<<packet.seqNum<<" length:"<<packet.datalength<<
            // " flags:"<<packet.flags<<" rwnd:"<<rwnd<<endl;
        }else{
            if(CheckFinished()){
                pthread_cond_signal(&isRecvAvailable);
            }
        }
    }
    return 0;
}

int TCPreceiver::appRecv(unsigned char* buf, unsigned long long size){
    if(CheckFinished()){
        return -1;
    }
    pthread_mutex_lock(&mutexLock);
    // cout<<LastByteRead<<" "<<NextByteExpected<<endl;
    while(LastByteRead>=NextByteExpected){
        pthread_cond_wait(&isRecvAvailable, &mutexLock);
        if(connectionState==CLOSE_WAIT && LastByteRead>=NextByteExpected){
            pthread_mutex_unlock(&mutexLock);
            return -1;
        }
    }
    unsigned long long len = min(NextByteExpected-LastByteRead, size);
    for(unsigned long long i=0; i<len; i++){
        buf[i] = tcpbuf[(LastByteRead + i) % bufsize];
    }
    LastByteRead += len;
    rwnd += len;
    if(rwnd-len<=MSS){
        TCPAckPacket ackpkg;
        ackpkg.ackNum = NextByteExpected;
        ackpkg.rwnd = rwnd;
        sendto(sockfd, &ackpkg, sizeof(ackpkg), 0, (struct sockaddr*)&senderaddr, sizeof(senderaddr));
    }
    // cout<<"appRecv len:"<<len<<" rwnd:"<<rwnd<<endl;
    pthread_mutex_unlock(&mutexLock);
    return len;
}

int TCPreceiver::CheckFinished(){
    pthread_mutex_lock(&mutexLock);
    // cout<<(connectionState==CLOSE_WAIT)<<endl;
    // cout<<connectionState<<endl;
    if(connectionState==CLOSE_WAIT && LastByteRead>=NextByteExpected){
        pthread_mutex_unlock(&mutexLock);
        cout<<"Check FIN"<<endl;
        return 1;
    }
    pthread_mutex_unlock(&mutexLock);
    return 0;
}

TCPreceiver TCPReceiver;

void* recvdemon(void* ignore){
    TCPReceiver.RecvDaemon();
    return NULL;
}

void diep(char *s) {
    perror(s);
    exit(1);
}

// open the socket 
struct sockaddr_in si_other, si_me;
int slen, socketfd;

int main(int argc, char** argv){
    
    // if (argc != 3) {
    //     fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
    //     exit(1);
    // }

    char* portstr = argv[1];
    char* destinationFile = argv[2];

    // char* portstr = const_cast<char*>("33449");
    // char* destinationFile = const_cast<char*>("unix.pdf");

    unsigned short int udpPort = (unsigned short int) atoi(portstr);
    


    slen = sizeof (si_other);
    if ((socketfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep(const_cast<char*>("socket"));
    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(udpPort);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(socketfd, (struct sockaddr*) &si_me, sizeof (si_me)) == -1)
        diep("bind");
   
    TCPReceiver.SetSocket(socketfd);

    // open the thread
    pthread_t  rcv_thread;
    int ret = pthread_create(&rcv_thread, NULL, recvdemon, NULL);
    if (ret != 0) {
        fprintf(stderr, "rcv_thread failed\n");
        exit(1);
    }

    //Open the file
    FILE *fp;
    fp = fopen(destinationFile, "wb");
    if (fp == NULL) {
        printf("Could not open file to receive.");
        exit(1);
    }

    // receive the packet
    size_t num_read_bytes = 0;
    unsigned char* recvbuf = new unsigned char[MSS+1];
    // num_read_bytes = TCPReceiver.appRecv(recvbuf, MSS);
    while((num_read_bytes = TCPReceiver.appRecv(recvbuf, MSS))!=-1){
        // cout<<"write: "<<num_read_bytes<<endl;
        // printbuf((char*)recvbuf, num_read_bytes);
        fwrite(recvbuf, 1, num_read_bytes, fp);
    }
    cout<<"exit"<<endl;
    if(num_read_bytes == 0){
        printf("receive complete");
    }

    close(socketfd);
    // fclose(fp);
    return 0;
}