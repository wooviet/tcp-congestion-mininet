#pragma once
#include <string>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <event2/event_struct.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/util.h>
#include "base/base_thread.h"
#include "base/random.h"
namespace tcp{
class NetworkThread;
class ActiveClientCounter{
public:
	virtual void Decrease()=0;
	virtual ~ActiveClientCounter(){}
};
class TcpClient{
public:
	TcpClient(NetworkThread* thread,ActiveClientCounter *counter,const char*serv_ip,
			uint16_t port,std::string &cc_algo);
	~TcpClient();
	void Bind(const char *local_ip);
    void SetSendBufSize(int len);
    void SetRecvBufSize(int len);
	void setSenderInfo(uint32_t cid,uint32_t length);
	void AsynConnect();
	void SynConnect();
	void NotifiConnect();
	void StopUseOnce();
	void NotifiRead();
	void NotifiWrite();
	void NotifiError(short event);
	void NextWriteEvent(int micro);
	void Close();
	void BufferFree();
	bool IsBulkReceivedByPeer(){return recv_ack_;}
private:
	void setCongestionAlgo();
	void IncreaseWriteBytes(int size);
	int GetBatchSize();
	int WritePacketInBatch(int length);
    void StopConectionThread();
    void NotifiDeactiveMsg();
	uint32_t client_id_{0};
	NetworkThread *thread_;
	ActiveClientCounter *counter_{nullptr};
	int sockfd_{-1};
	struct sockaddr_in servaddr_;
	base::BaseThread UsedOnce_;
	struct event write_event_;
	struct bufferevent *bev_{nullptr};
	bool asyconnect_{false};
	bool connected_{false};
	base::Random random_;
	bool first_sent_{false};
	bool recv_ack_{false};
	uint32_t sendByte_{0};
	uint32_t totalByte_{0};
	std::string cc_algo_{"cubic"};
	bool deactive_sent_{false};
};
}
