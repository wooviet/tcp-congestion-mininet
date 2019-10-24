#include <time.h>
#include <string.h>
#include <memory.h>
#include <unistd.h>
#include <algorithm>
#include "base/byte_codec.h"
#include "logging.h"
#include "tcp_client.h"
#include "network_thread.h"
#include "logging.h"
namespace tcp{
const int MAX_LINE=1400;
const int kPacketBatchSize=20;
static const size_t TCP_CC_NAME_MAX = 16;
void TcpClientThread(void *arg){
	TcpClient *client=static_cast<TcpClient*>(arg);
	client->SynConnect();
}
void WriteEventCallback(evutil_socket_t fd, short event, void *arg){
	TcpClient *client=static_cast<TcpClient*>(arg);
	client->NotifiWrite();
}
void ReadEventCallback(struct bufferevent *bev, void *arg){
	TcpClient *client=static_cast<TcpClient*>(arg);
	client->NotifiRead();
}
void ErrorCallback(struct bufferevent *bev, short event, void *arg){
	TcpClient *client=static_cast<TcpClient*>(arg);
	client->NotifiError(event);
}
TcpClient::TcpClient(NetworkThread* thread,ActiveClientCounter *counter,const char*serv_ip,
		uint16_t port,std::string &cc_algo)
:UsedOnce_(TcpClientThread,this){
	thread_=thread;
	counter_=counter;
	cc_algo_=cc_algo;
    bzero(&servaddr_, sizeof(servaddr_));
    servaddr_.sin_family = AF_INET;
    servaddr_.sin_port = htons(port);
    if(inet_pton(AF_INET, serv_ip, &servaddr_.sin_addr) < 1)
    {
    	LOG(INFO)<<"inet_ntop\n";
        return ;
    }
}
TcpClient::~TcpClient(){
	Close();
	BufferFree();
}
void TcpClient::Bind(const char *local_ip){
    if((sockfd_= socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        LOG(INFO)<<"socket\n";
        return;
    }
    struct sockaddr_in localaddr;
    bzero(&localaddr, sizeof(localaddr));
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = inet_addr("192.168.1.100");
    localaddr.sin_port = 0;  // Any local port will do
    if(inet_pton(AF_INET, local_ip, &localaddr.sin_addr) < 1)
    {
    	LOG(INFO)<<"inet_ntop\n";
        return ;
    }
    bind(sockfd_, (struct sockaddr *)&localaddr, sizeof(localaddr));
	setCongestionAlgo();
}
void TcpClient::SetSendBufSize(int len){
    if(sockfd_<0){
        return ;
    }
    
    int nSndBufferLen =len;
    int nLen          = sizeof(int);
    setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, (char*)&nSndBufferLen, nLen);
}
void TcpClient::SetRecvBufSize(int len){
    if(sockfd_<0){
        return ;
    }
    int nRcvBufferLen =len;
    int nLen          = sizeof(int);
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, (char*)&nRcvBufferLen, nLen);
}
void TcpClient::setSenderInfo(uint32_t cid,uint32_t length){
	client_id_=cid;
	if(length<MAX_LINE){
		length=MAX_LINE;
	}
	totalByte_=length;
}
void TcpClient::AsynConnect(){
	if(sockfd_>0){
		asyconnect_=true;
		UsedOnce_.Start();
	}
}
void TcpClient::SynConnect(){
	struct event_base *evb=thread_->getEventBase();
	if(sockfd_<0){
		LOG(INFO)<<"Call Bind First";
        StopConectionThread();
		return ;
	}
    if(connect(sockfd_, (struct sockaddr *) &servaddr_, sizeof(servaddr_)) < 0)
    {
    	LOG(INFO)<<"connect error";
    }else{
    	connected_=true;
    	LOG(INFO)<<"connect success";
    }
    thread_->PostTask([this](){
		NotifiConnect();
	});
    StopConectionThread();
}
void TcpClient::StopConectionThread(){
	if(asyconnect_){
        asyconnect_=false;
		thread_->PostTask([this](){
			StopUseOnce();
		});
	}
}
void TcpClient::StopUseOnce(){
	UsedOnce_.Stop();
}
void TcpClient::NotifiConnect(){
	if(connected_){
		random_.seedTime();
		struct event_base *evb=thread_->getEventBase();
    	evutil_make_socket_nonblocking(sockfd_);
    	bev_=bufferevent_socket_new(evb, sockfd_, BEV_OPT_CLOSE_ON_FREE);
    	bufferevent_setcb(bev_, ReadEventCallback, NULL, ErrorCallback, (void *)this);
    	bufferevent_enable(bev_, EV_READ | EV_PERSIST);
    	NextWriteEvent(0);
	}
}
void TcpClient::NotifiRead(){
    char line[MAX_LINE + 1];
    int n;
    while((n = bufferevent_read(bev_, line, MAX_LINE)) > 0)
    {
        line[n] = '\0';
        if(!recv_ack_){
        	recv_ack_=true;
        }
    }
    if(recv_ack_){
    	LOG(INFO)<<"trans success";
    	Close();
    	NotifiDeactiveMsg();
    }
}
void TcpClient::NotifiWrite(){
	if(sockfd_<=0){
		return;
	}
	char msg[MAX_LINE]={0};
	int ret=0;
	if(!first_sent_){
		basic::DataWriter writer(msg,MAX_LINE);
		writer.WriteUInt32(client_id_);
		writer.WriteUInt32(totalByte_);
	    ret=bufferevent_write(bev_, msg, MAX_LINE);
	    if(ret==0){
	    	IncreaseWriteBytes(MAX_LINE);
	    }
		first_sent_=true;
		int batch=GetBatchSize()*3;
	    int write=WritePacketInBatch(batch);
	    IncreaseWriteBytes(write);
	}else{
		int batch=GetBatchSize();
	    int write=WritePacketInBatch(batch);
	    IncreaseWriteBytes(write);
	}
	if(sendByte_<totalByte_){
		uint32_t micro=random_.nextInt(0,1000); //0-1 ms
		NextWriteEvent(micro);
	}
}
void TcpClient::NotifiError(short event){
    if (event & BEV_EVENT_TIMEOUT) {
        printf("Timed out\n");
    }
    else if (event & BEV_EVENT_EOF) {
        printf("connection closed\n");
        Close();
    }
    else if (event & BEV_EVENT_ERROR) {
        printf("some other error\n");
        Close();
    }
}
void TcpClient::NextWriteEvent(int micro){
	struct timeval tv;
	struct event_base *evb=thread_->getEventBase();
	event_assign(&write_event_, evb, -1, 0,WriteEventCallback, (void*)this);
	evutil_timerclear(&tv);
	tv.tv_sec = micro/(1000*1000);
	tv.tv_usec=micro%(1000*1000);
	event_add(&write_event_, &tv);
}
void TcpClient::Close(){
	NotifiDeactiveMsg();
	if(sockfd_>0){
		evutil_closesocket(sockfd_);
		sockfd_=0;
	}
}
void TcpClient::BufferFree(){
	if(bev_){
		bufferevent_free(bev_);
		bev_=nullptr;
	}
}
void TcpClient::setCongestionAlgo(){
    char optval[TCP_CC_NAME_MAX]={0};
    memset(optval,0,TCP_CC_NAME_MAX);
    int copy=std::min(TCP_CC_NAME_MAX,cc_algo_.size());
    strncpy(optval,cc_algo_.c_str(),copy);
    int length=strlen(optval)+1;
    LOG(INFO)<<"cc "<<sockfd_<<" "<<optval;
    int rc=setsockopt(sockfd_,IPPROTO_TCP, TCP_CONGESTION, (void*)optval,length);
    if(rc!=0){
	LOG(INFO)<<cc_algo_<<" is not support";
	}
}
void TcpClient::IncreaseWriteBytes(int size){
	sendByte_+=size;
}
int TcpClient::GetBatchSize(){
	int max_batch=kPacketBatchSize*MAX_LINE;
	int remain=totalByte_-sendByte_;
	int batch=std::min(max_batch,remain);
	return batch;
}
int TcpClient::WritePacketInBatch(int length){
	char msg[MAX_LINE]={0};
	int total=0;
	int remain=length;
	while(remain>0){
		int write=std::min(remain,MAX_LINE);
		int ret=0;
	    ret=bufferevent_write(bev_, msg, write);
	    if(ret==0){
	    	total+=write;
	    	remain-=write;
	    }else{
	    	LOG(INFO)<<client_id_<<" write error";
	    	break;
	    }
	}
	return total;
}
void TcpClient::NotifiDeactiveMsg(){
	if(!deactive_sent_){
    	if(counter_){
    		counter_->Decrease();
    	}
		deactive_sent_=true;
	}
}
}
