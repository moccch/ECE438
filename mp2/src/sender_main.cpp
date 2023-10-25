#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>

#include <cstdint>
#include <deque>
#include <ctime>
#include <string>
#include <iostream>
#include <unordered_map>

using namespace std;

// #define BUFSIZE 1024
#define MSS     ((unsigned long long)1460)
#define TCP_HEADER_SIZE ((unsigned long long)12)
#define TCP_PACKET_SIZE (MSS + TCP_HEADER_SIZE)
#define DEFAULT_TIMEOUT_INTERVAL 300 // Unit: ms
#define DEFAULT_RWND (5*MSS)
#define DEFAULT_CWND (2*MSS)
// #define DEFAULT_SSTHRESH ((unsigned long long)(64<<10))
#define DEFAULT_SSTHRESH (20*MSS)

#define FIN_FLAG 1



enum CongestionState{
    SlowStart,
    FastRecovery,
    CongestionAvoidance
};

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

class TCPsender{
public:
    // Lock & Cond
    pthread_mutex_t mutexLock;
    pthread_cond_t isSendAvailable;
    // Connection Info
    int sockfd;
    struct sockaddr_in recvaddr;
    // Reliable Data Transfer
    unsigned long long NextSeqNum;
    unsigned long long SendBase;
    unsigned long long LastByteSent;
    unsigned long long dupACKcount;
    deque<TCPSendPacket*> SendQueue;
    unordered_map<long long, clock_t> packetTime;
    // Timeout
    unsigned long long dupAckVal;
    bool isClockStart;
    // unit: ms
    unsigned long long clockStartTime;
    unsigned long long timeoutInterval;
    unsigned long long defaultTimeout;

    struct timeval TimeOut;
    struct timeval TimeStart;

    // RTT
    long long estimatedRTT;
    long long devRTT;
    double alpha;
    double beta;
    // Flow Control
    unsigned long long rwnd;
    // Congestion Control
    CongestionState congestionState;
    unsigned long long ssthresh;
    unsigned long long cwnd;
    // Connection
    ConnectionState connectionState;
public:
    TCPsender();
    int SetSocket(int socketfd, struct sockaddr_in* recvinfo);
    // Reliable Data Transfer
    int appSend(unsigned char* buf, unsigned long long size);   //返回一共几个packet
    int makePacket(unsigned char* buf, unsigned long long size, uint32_t flags);
    int recvACK(TCPAckPacket* packet);
    void RecvDaemon();
    int TimeoutHandler();
    int windowCheck(unsigned long long pos);
    // Timeout
    void ClockDaemon();
    int ClockStart();
    int ClockStop();
    // RTT
    int updateRTT(long long curRTT);
    int updateRTT();
    // Connection
    int closeConnection();
    int CheckFinished();
    int updateCWND(unsigned long long newCWND);
    int retransWindow();
    // Event Loop
    int eventLoop();
};

TCPsender::TCPsender(){
    mutexLock = PTHREAD_MUTEX_INITIALIZER;
    isSendAvailable = PTHREAD_COND_INITIALIZER;
    // Connection Info
    // sockfd = socketfd;
    // recvaddr = *recvinfo;
    // Reliable Data Transfer
    NextSeqNum = 0;
    SendBase = 0;
    LastByteSent = 0;
    dupACKcount = 0;
    // Timeout
    isClockStart = false;
    clockStartTime = 0;
    timeoutInterval = DEFAULT_TIMEOUT_INTERVAL;
    defaultTimeout = DEFAULT_TIMEOUT_INTERVAL;
    // RTT
    estimatedRTT = 200;
    devRTT = 0;
    alpha = 0.125;
    beta = 0.25;
    // Flow Control
    rwnd = DEFAULT_RWND;
    // Congestion Control
    congestionState = SlowStart;
    ssthresh = DEFAULT_SSTHRESH;
    cwnd = DEFAULT_CWND;
    // Connection
    connectionState = ESTABLISHED;
}

int getTime(){
    return (unsigned long long)clock()*1000/CLOCKS_PER_SEC;
}

int setSocketTimeout(int sockfd, int timeout){
    struct timeval socketTime;
    socketTime.tv_sec = timeout / 1000;
    socketTime.tv_usec = timeout * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&socketTime, sizeof(socketTime));
}

int TCPsender::eventLoop(){
    // TCPAckPacket packet;
    // struct sockaddr_in peeraddr;
    // socklen_t addrlen = sizeof(peeraddr);
    // clockStartTime = getTime();
    // while(1){
    //     int timepass = getTime() - clockStartTime;
    //     setSocketTimeout(timeoutInterval - timepass);
    //     if(recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&peeraddr, &addrlen) > 0){
    //         recvACK(&packet);
    //     }else{
    //         updateRTT(getTime() - clockStartTime);
    //     }
    // }
}





int TCPsender::SetSocket(int socketfd, struct sockaddr_in* recvinfo){
    // Connection Info
    sockfd = socketfd;
    recvaddr = *recvinfo;
    return 0;
}

// Reliable Data Transfer:
int TCPsender::appSend(unsigned char* buf, unsigned long long size){
    unsigned long long index = 0;
    unsigned long long cnt = 0;
    while(index<size){
        unsigned long long cursize = min(MSS, size-index);
        makePacket(buf+index, cursize, 0);
        index += cursize;
        cnt ++;
    }
    return cnt;
}

int TCPsender::makePacket(unsigned char* buf, unsigned long long size, uint32_t flags){
    int windowavailable = 0;
    pthread_mutex_lock(&mutexLock);
    while(!windowCheck(LastByteSent+size)){
        pthread_cond_wait(&isSendAvailable, &mutexLock);
    }
    TCPSendPacket* packet = new TCPSendPacket;
    size = min(size, MSS);
    if(flags & FIN_FLAG){
        packet->flags = FIN_FLAG;
    }
    packet->seqNum = NextSeqNum;
    packet->datalength = size;
    // cout<<"PacketSend Seq:"<<(packet->seqNum/MSS)<<" cwmd:"<<((double)cwnd/MSS)<<endl;
    memcpy(packet->data, buf, size);
    if(!isClockStart){
        ClockStart();
    }
    sendto(sockfd, packet, sizeof(*packet), 0, (struct sockaddr*)&recvaddr, sizeof(recvaddr));
    
    cout<<"App Send Seq:"<<(packet->seqNum/MSS)<<endl;
    if(SendQueue.empty()){
        ClockStart();
    }
    SendQueue.push_back(packet);
    packetTime.insert(make_pair(packet->seqNum+packet->seqNum, clock()));
    NextSeqNum += size;
    LastByteSent = NextSeqNum - 1;
    pthread_mutex_unlock(&mutexLock);
    return size;
}

int TCPsender::recvACK(TCPAckPacket* packet){
    // Flow Control
    pthread_mutex_lock(&mutexLock);
    rwnd = packet->rwnd;
    cout<<"\n                                                                                              "
    <<"ACK Pkg: "<<(packet->ackNum/MSS-1)<<"; Queue: "<<SendQueue.size()
    <<"\n"<<endl;
    if(packet->ackNum > SendBase){
        if(packetTime.find(packet->ackNum)!=packetTime.end()){
            updateRTT((long long)(clock()-packetTime[packet->ackNum])*1000/CLOCKS_PER_SEC);
        }
        packetTime.erase(packet->ackNum);
        updateRTT();
        SendBase = packet->ackNum;
        clock_t pkgtime;
        while(1){
            if(SendQueue.empty()) break;
            TCPSendPacket* ptr = SendQueue.front();
            if(ptr->seqNum>=SendBase) break;
            // pkgtime = timeQueue.front();
            delete ptr;
            SendQueue.pop_front();
            // timeQueue.pop_front();
        }
        // updateRTT(((long long)(clock()-pkgtime))*1000/CLOCKS_PER_SEC);
        if(SendBase < NextSeqNum){
            ClockStart();
        }else{
            ClockStop();
        }
        // Congestion Control
        // New ACK
        // timeoutInterval = defaultTimeout;
        switch(congestionState){
            case SlowStart:
                updateCWND(cwnd+MSS);
                dupACKcount = 0;
                if(cwnd>=ssthresh){
                    congestionState = CongestionAvoidance;
                }
                break;
            case CongestionAvoidance:
                cout<<cwnd + MSS * MSS / cwnd<<endl;
                updateCWND(cwnd + MSS * MSS / cwnd);
                dupACKcount = 0;
                break;
            case FastRecovery:
                updateCWND(ssthresh);
                dupACKcount = 0;
                congestionState = CongestionAvoidance;
                break;
        }
        pthread_cond_signal(&isSendAvailable);
    }else{
        // Duplicated ACK
        if(packet->ackNum==SendBase){
            dupACKcount ++;
            // Congestion Control
            // Duplicated ACK == 3
            switch(congestionState){
                case SlowStart:
                    if(dupACKcount>=3){
                        ssthresh = cwnd / 2;
                        updateCWND(ssthresh + 3 * MSS);
                        congestionState = FastRecovery;
                    }
                    break;
                case CongestionAvoidance:
                    if(dupACKcount>=3){
                        ssthresh = cwnd / 2;
                        updateCWND(ssthresh + 3 * MSS);
                        congestionState = FastRecovery;
                    }
                    break;
                case FastRecovery:
                    // dupACKcount = 3;
                    updateCWND(cwnd + MSS);
                    break;
            }
            if(dupACKcount == 3){
                if(!SendQueue.empty()){
                    TCPSendPacket* ptr = SendQueue.front();
                    cout<<"<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"<<endl;
                    cout<<"DupACK Retrans Seq: ";
                    packetTime[ptr->seqNum+ptr->datalength] = clock();
                    sendto(sockfd, ptr, sizeof(*ptr), 0, (struct sockaddr*)&recvaddr, sizeof(recvaddr));
                    ClockStart();
                    // retransWindow();
                    cout<<endl;
                }else{
                    pthread_cond_signal(&isSendAvailable);
                }
            }
            if(SendQueue.empty()){
                pthread_cond_signal(&isSendAvailable);
            }
            // cout<<"Dup ACK:"<<(packet->ackNum/MSS)<<" dupack:"<<dupACKcount<<
            // " cwnd:"<<(cwnd/MSS)<<" congState:"<<congestionState<<endl;
        }
    }
    // cout<<"congState: "<<congestionState<<" ack:"<<(packet->ackNum/MSS)<<endl;
    // if(windowCheck(MSS)){
    // }
    pthread_mutex_unlock(&mutexLock);
    return 0;
}

void TCPsender::RecvDaemon(){
    TCPAckPacket packet;
    struct sockaddr_in peeraddr;
    socklen_t addrlen = sizeof(peeraddr);
    while(1){
        if(recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&peeraddr, &addrlen)>0){
            recvACK(&packet);
        };
    }
}

int TCPsender::TimeoutHandler(){
    pthread_mutex_lock(&mutexLock);
    // Reliable Data Transfer
    TCPSendPacket* packet = SendQueue.front();
    if(SendQueue.empty()){
        pthread_mutex_unlock(&mutexLock);
        return 0;
    }
    // Congestion Control
    ssthresh = cwnd / 2;
    cwnd = MSS;
    dupACKcount = 0;
    timeoutInterval += defaultTimeout;
    congestionState = SlowStart;
    // cout<<"Timeout SeqNum:"<<(packet->seqNum/MSS)<<" cwnd:"<<(cwnd/MSS)<<endl;
    
    // Resend first packet
    sendto(sockfd, packet, sizeof(*packet), 0, (struct sockaddr*)&recvaddr, sizeof(recvaddr));
    cout<<">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"<<endl;
    cout<<"Timeout Retrans Seq: "<<(packet->seqNum/MSS)<<endl;
    // usleep(1000*1000);
    // Clock
    packetTime[packet->seqNum+packet->datalength] = clock();
    ClockStart();
    pthread_mutex_unlock(&mutexLock);
    return 0;
}

int TCPsender::windowCheck(unsigned long long pos){
    if(pos-SendBase <= min(cwnd, rwnd)){
        return 1;
    }
    return 0;
}


long long sumCWND = 0;
long long cntCWND = 0;
int TCPsender::updateCWND(unsigned long long newCWND){
    int cnt = 0;

    sumCWND += newCWND/MSS;
    cntCWND ++;
    cout<<"Avg CWND: "<<(sumCWND/cntCWND)<<"; ";
    cout<<"state: "<<congestionState<<"; cwnd: "<<(newCWND/MSS)<<";\n Retrans:";
    if(newCWND<=cwnd){
        cwnd = newCWND;
    }else{
        auto it = SendQueue.begin();
        for(;it!=SendQueue.end();it++){
            if((*it)->seqNum+(*it)->datalength >= SendBase+cwnd){
                break;
            }
        }
        for(;it!=SendQueue.end(); it++){
            // if(it==SendQueue.begin()){
            //     ClockStart();
            // }
            if((*it)->seqNum + (*it)->datalength >= SendBase + min(rwnd, newCWND)){
                break;
            }
            cnt ++;
            sendto(sockfd, (*it), sizeof(*(*it)), 0, (struct sockaddr*)&recvaddr, sizeof(recvaddr));
            cout<<((*it)->seqNum/MSS)<<" ";
        }
        cwnd = newCWND;
        pthread_cond_signal(&isSendAvailable);
    }
    cout<<";  Total "<<cnt<<" packets; ";
    cout<<"Window: ["<<(SendBase/MSS)<<" ,"<<((SendBase+min(rwnd,cwnd))/MSS-1)<<"];";
    cout<<" Pkg in Queue: "<<SendQueue.size()<<" Clock: "<<isClockStart<<"\n"<<endl;
    return 0;
}

int TCPsender::retransWindow(){
    int cnt = 0;
    for(auto it=SendQueue.begin(); it!=SendQueue.end(); it++){
        if(cnt==1) break;
        cnt++;
        if((*it)->seqNum + (*it)->datalength >= SendBase + min(rwnd, cwnd)){
            break;
        }
        cout<<((*it)->seqNum/MSS)<<" ";
        sendto(sockfd, (*it), sizeof(*(*it)), 0, (struct sockaddr*)&recvaddr, sizeof(recvaddr));
    }
    ClockStart();
    return 0;
}

// RTT
int TCPsender::updateRTT(long long sampleRTT){
    if(sampleRTT<100) return 0;
    estimatedRTT = (1-alpha)*estimatedRTT + alpha*sampleRTT;
    devRTT = (1-beta)*devRTT + beta*(abs(sampleRTT-estimatedRTT));
    // estimatedRTT = 200;
    // devRTT = 0;
    timeoutInterval = estimatedRTT + 4*devRTT;
    // timeoutInterval = DEFAULT_TIMEOUT_INTERVAL;
    // cout<<"sampleRTT="<<sampleRTT
    // <<" estRTT="<<estimatedRTT<<" timeoutInterval="<<timeoutInterval<<endl;
    return 0;
}

int TCPsender::updateRTT(){
    // timeoutInterval = estimatedRTT + 4*devRTT;
    timeoutInterval = DEFAULT_TIMEOUT_INTERVAL;
    return 0;
}

// Timeout
void TCPsender::ClockDaemon(){
    while(1){
        if(CheckFinished()){
            break;
        }
        if(isClockStart){
            // cout<<clockStartTime<<endl;
            if(((unsigned long long)(clock()-clockStartTime)/(unsigned long long)CLOCKS_PER_SEC*1000.0) > timeoutInterval){
                // cout<<"timeout time in ms:"<<((double)(clock()-clockStartTime)*1000/CLOCKS_PER_SEC)
                // <<" TimeoutInterval:"<<timeoutInterval<<endl;
                TimeoutHandler();
            }
        }
    }
}

int TCPsender::ClockStart(){
    clockStartTime = clock();
    isClockStart = true;
    return 0;
}

int TCPsender::ClockStop(){
    isClockStart = false;
    return 0;
}

int TCPsender::closeConnection(){
    makePacket((unsigned char*)"", 0, FIN_FLAG);
    connectionState = CLOSE_WAIT;
    return 0;
}

int TCPsender::CheckFinished(){
    pthread_mutex_lock(&mutexLock);
    if(SendQueue.empty() && connectionState==CLOSE_WAIT){
        pthread_mutex_unlock(&mutexLock);
        return 1;
    }
    pthread_mutex_unlock(&mutexLock);
    return 0;
}

TCPsender TcpSender;

void* clockdemon(void* ignore){
    TcpSender.ClockDaemon();
    return NULL;
}

void* recvdemon(void* ignore){
    TcpSender.RecvDaemon();
    return NULL;
}


void diep(char *s) {
    perror(s);
    exit(1);
}

int main(int argc, char** argv){
    // TcpSender

    // if (argc != 5) {
    //     fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
    //     exit(1);s
    // }

    char* hoststr = argv[1];
    char* portstr = argv[2];
    char* filenamestr = argv[3];
    // char* numBytesstr = argv[4];
    clock_t sendstart = clock();

    // char* hoststr = const_cast<char*>("192.168.116.128");
    // char* portstr = const_cast<char*>("33449");
    // char* filenamestr = const_cast<char*>("linuxker.pdf");

    char* hostname = hoststr;
    unsigned short int udpPort = (unsigned short int) atoi(portstr);
    char* filename = filenamestr;

    // open the socket 
    struct sockaddr_in si_other;
    int slen, socketfd;
    slen = sizeof (si_other);
    if ((socketfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep(const_cast<char*>("socket"));
    memset((char *) &si_other, 0, slen);
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(udpPort);


    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }
    TcpSender.SetSocket(socketfd, &si_other);

    // open the thread
    pthread_t clock_thread, rcv_thread;
    int ret = pthread_create(&clock_thread, NULL, clockdemon, NULL);
    if (ret != 0) {
        fprintf(stderr, "clock_thread failed\n");
        exit(1);
    }
    int ret2 = pthread_create(&rcv_thread, NULL, recvdemon, NULL);
    if (ret2 != 0) {
        fprintf(stderr, "rcv_thread failed\n");
        exit(1);
    }

    //Open the file
    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Could not open file to send.");
        exit(1);
    }

    size_t num_read_bytes = 0;
    unsigned char* sendbuf = new unsigned char[MSS+1];
    // sleep(500);
    while(num_read_bytes = fread(sendbuf, 1, MSS, fp)){
        TcpSender.appSend(sendbuf, num_read_bytes);
    }
    TcpSender.closeConnection();
    if(num_read_bytes == 0){
        printf("sender complete");
    }

    // pthread_join(clock_thread, NULL);
	close(socketfd);

    cout<<"Time to send: "<<((double)(clock()-sendstart)/CLOCKS_PER_SEC)<<endl;
    return 0;
}
