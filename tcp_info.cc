#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/types.h> 
#include <netinet/tcp.h>  //struct tcp_info
#include <string>
#include <algorithm>
#include "logging.h"
static const size_t TCP_CC_NAME_MAX = 16;
void error_handling(char *message);
void print_cc_type(int fd){
    char optval[TCP_CC_NAME_MAX];
    memset(optval,0,TCP_CC_NAME_MAX);
    int length=sizeof(optval);
    getsockopt(fd,IPPROTO_TCP, TCP_CONGESTION, (void*)optval,(socklen_t*)&length);
    printf("cctype %s\n",optval);
}
void print_tcp_info(int fd){
    struct tcp_info info;
    int length=sizeof(struct tcp_info);
     if(getsockopt(fd,IPPROTO_TCP,TCP_INFO,(void*)&info,(socklen_t*)&length)==0){
        printf("cwnd %u ss %u\n",info.tcpi_snd_cwnd,info.tcpi_snd_ssthresh);
     }
}
int set_congestion_type(int fd,char *cc){
    char optval[TCP_CC_NAME_MAX];
    memset(optval,0,TCP_CC_NAME_MAX);
    strncpy(optval,cc,TCP_CC_NAME_MAX);
    int length=strlen(optval)+1;
    int rc=setsockopt(fd,IPPROTO_TCP, TCP_CONGESTION, (void*)optval,length);
    if(rc!=0){
        printf("cc is not supprt\n");
    }
    return rc;
}
int set_congestion_algo(int fd,std::string &cc_algo){
    char optval[TCP_CC_NAME_MAX]={0};
    memset(optval,0,TCP_CC_NAME_MAX);
    int copy=std::min(TCP_CC_NAME_MAX,cc_algo.size());
    strncpy(optval,cc_algo.c_str(),copy);
    int length=strlen(optval)+1;
    LOG(INFO)<<"cc "<<fd<<" "<<optval;
    int rc=setsockopt(fd,IPPROTO_TCP, TCP_CONGESTION, (void*)optval,length);
    if(rc!=0){
	LOG(INFO)<<"cc is not support";
    }
    return rc;
}
#include <iostream>
#include <event2/event.h>
#include "base/logging.h"
#include "base/proto_time.h"
#include "base/bandwidth.h"
using namespace base;
using namespace std;
int main(int argc, char* argv[])
{
    int sock;
    char *cc_type="cubic";
    sock=socket(PF_INET, SOCK_STREAM, 0);
    if(sock == -1)
        error_handling("socket() error");
    print_tcp_info(sock);
    //set_congestion_type(sock,cc_type);
    std::string cc_algo("cubic");
    set_congestion_algo(sock,cc_algo);
    print_cc_type(sock);
    close(sock);
    SystemClock clock;
    ProtoTime last=clock.Now();
    TimeSleep(1000);
    TimeDelta delta=clock.Now()-last;
    std::cout<<"sleeped: "<<delta.ToMilliseconds()<<std::endl;
    QuicBandwidth bw=QuicBandwidth::Zero();
    return 0;
}

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
