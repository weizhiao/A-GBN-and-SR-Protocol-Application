#include <stdlib.h>
#include<stdio.h>
#include <time.h>
#include <WinSock2.h>
#pragma comment(lib,"ws2_32.lib")

#define FUNC_QUIT 1	//退出
#define FUNC_TESTGBN 2//测试GBN
#define FUNC_TESTSR	3//测试SR
#define FUNC_TESTCS	4//测试双向通信
#define SEND_END 4	//发送结束
#define SR_SEND 0	//使用SR的发送报文
#define SR_ACK 1	//使用SR的确认报文
#define GBN_SEND 2	//使用GBN的发送报文
#define GBN_ACK 3	//使用GBN的确认报文
#define DATA_SIZE 1024*50 //文件读取的最大字节
#define SEQ_SIZE 20 //序列号的个数，从0~19 共计 20 个
#define BUFFER_SIZE 1026 //缓冲区大小，（以太网中 UDP 的数据帧中包长度应小于 1480 字节）
#define SEND_WIND_SIZE 10	//发送窗口大小为 10
#define RECV_WIND_SIZE 1	//接收窗口大小为 10
#define PROTOCALSIZE 1024	//协议内容大小

struct Protocal {
	unsigned char func;
	unsigned char seq;
	char content[PROTOCALSIZE];
};

BOOL ack[SEQ_SIZE];//收到 ack 情况，对应 0~19 的 ack
BOOL inSendCache[SEQ_SIZE];//是否在发送窗口
int TimeCount[SEQ_SIZE];//计时器
BOOL inRecvCache[RECV_WIND_SIZE];//缓存区的缓存情况
char recvBuffer[RECV_WIND_SIZE][PROTOCALSIZE];//接收缓存区
char sendBuffer[SEQ_SIZE][BUFFER_SIZE];//发送窗口
int curSendSeq;//当前发送数据包的seq
int curSendAck;//当前等待确认的最小序列
int curRecvAck;//接收区最小的序列号
int totalSeq;//发送的包总数
int totalAck;//收到的ack总数
int totalPacket;//需要发送的包总数
int totalRecv;//总共接收到的包总数
SOCKET Server;
const int ServerPort = 8080;
float packetLossRatio=0.1;//丢包率

//根据丢失率随机生成一个数字，判断是否丢失,丢失则返回TRUE，否则返回 FALSE
BOOL lossInLossRatio(float lossRatio);
//写文件
void Writefile(char* filename, char* data, int len);
//测试
int Test(struct sockaddr_in addrClient, struct Protocal* protocal, unsigned char type, int stage);
//封装协议到结构体中
void PacketProtocal(struct Protocal* protocal, unsigned char func, unsigned char seq, char* content);
//创建协议发送报文
void CreateProtocalHeader(struct Protocal* protocal, char* Buffer);
//解析协议报文
void ParseProtocalHeader(struct Protocal* protocal, char* Buffer);
//初始化套接字
int InitSocket();
//加载库
int LoadDLL();
//读取文件，返回读取文件的字节数
int Readfile(char* filename, char* data, int len);
//当前序列是否在发送窗口中
int seqIsAvailable();
//是否在发送窗口中
int IfinSendWind(const unsigned char seq);
//将缓存区中数据移出
void RemoveRecvBuffer(char* data);
//超时处理
void timeHandler(unsigned char type);
//是否发送ack
int IfSendAck(const unsigned char seq);
//返回功能码
int func(char* buffer);
//将收到的数据存入接收窗口
void SaveRecv(int seq, char* content);
//处理接受的协议报文
int RecvHandler(SOCKET Server, struct Protocal* protocal, struct sockaddr_in* addrClient, char* buffer);
//处理发送的协议报文
int SendHandler(SOCKET Server, struct Protocal* protocal, struct sockaddr_in* addrClient, char* data, char* buffer,unsigned char type);
//服务器提供的服务
int Service();

//主函数
int main(int argc, char* argv[])
{
	printf("正在加载套接字库...\n");
	if (LoadDLL() == FALSE) {
		printf("加载套接字库失败\n");
		return -1;
	}
	printf("加载成功！\n");
	printf("正在初始化服务器...\n");
	if (InitSocket() == FALSE) {
		printf("初始化服务器的套接字端口失败\n");
		return -1;
	}
	printf("初始化完成！\n");
	Service();
	//关闭套接字，卸载库
	closesocket(Server);
	WSACleanup();
	return 0;
}

//创建协议结构体
void PacketProtocal(struct Protocal* protocal, unsigned char func, unsigned char seq, char* content) {
	ZeroMemory(protocal, sizeof(struct Protocal));
	protocal->func = func;
	protocal->seq = seq;
	memcpy(protocal->content, content, PROTOCALSIZE);
}

//创建协议报文
void CreateProtocalHeader(struct Protocal* protocal, char* Buffer) {
	int current = 0;
	memcpy(Buffer, &protocal->func, 1);
	current++;
	memcpy(&Buffer[current++], &protocal->seq, 1);
	memcpy(&Buffer[current], protocal->content, PROTOCALSIZE);
}

//解析协议报文
void ParseProtocalHeader(struct Protocal* protocal, char* Buffer) {
	memcpy(&protocal->func, Buffer, 1);
	memcpy(&protocal->seq, &Buffer[1], 1);
	memcpy(protocal->content, &Buffer[2], PROTOCALSIZE);
}

//初始化服务器端套接字
int InitSocket() {
	Server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (INVALID_SOCKET == Server) {
		printf("创建套接字失败，错误代码为：%d\n", WSAGetLastError());
		return FALSE;
	}
	struct sockaddr_in ServerAddr;
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(ServerPort);
	//INADDR_ANY代表本机
	ServerAddr.sin_addr.s_addr = INADDR_ANY;
	int iMode = 1; //1：非阻塞，0：阻塞
	ioctlsocket(Server, FIONBIO, (u_long FAR*) & iMode);//非阻塞设置
	if (bind(Server, (SOCKADDR*)&ServerAddr, sizeof(SOCKADDR)) == SOCKET_ERROR) {
		printf("绑定套接字失败\n");
		return FALSE;
	}
	return TRUE;
}

//加载套接字库
int LoadDLL() {
	WORD wVersionRequested;
	WSADATA wsaData;
	//套接字加载时错误提示
	int err;
	//版本 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//加载 dll 文件 Scoket 库
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//找不到 winsock.dll
		printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("不能找到正确的 winsock 版本\n");
		WSACleanup();
		return FALSE;
	}
	return TRUE;
}

//写文件
void Writefile(char* filename, char* data, int len) {
	FILE* fp;
	fopen_s(&fp, "recv.txt", "w");
	fwrite(data, 1, len, fp);
	fclose(fp);
}

//读取文件
int Readfile(char* filename, char* data, int len) {
	FILE* fp;
	if (fopen_s(&fp, filename, "r") != 0) {
		printf("无法打开文件%s\n", filename);
		return -1;
	}
	int current = 0;
	char ch;
	while ((ch = fgetc(fp)) != EOF) {
		if (len == current) {
			printf("内存大小不足，无法读取整个文件!\n");
			break;
		}
		data[current++] = ch;
	}
	fclose(fp);
	return current;
}

//当前序列号 curSeq 是否可用
int seqIsAvailable() {
	int step;
	step = curSendSeq - curSendAck;
	step = step >= 0 ? step : step + SEQ_SIZE;
	//序列号是否在当前发送窗口之内
	if (step >= SEND_WIND_SIZE) {
		return FALSE;
	}
	if (ack[curSendSeq]) {
		return TRUE;
	}
	return FALSE;
}

//判断序号是否在发送窗口内
int IfinSendWind(const unsigned char seq) {
	int step;
	step = seq - curSendAck;
	step = step >= 0 ? step : step + SEQ_SIZE;
	if (step >= SEND_WIND_SIZE) {
		return FALSE;
	}
	return TRUE;
}

//将缓存区中数据移出
void RemoveRecvBuffer(char* data) {
	int i;
	for (i = 0; inRecvCache[i] == TRUE; i++) {
		memcpy(data + PROTOCALSIZE * totalRecv, recvBuffer[i], PROTOCALSIZE);
		curRecvAck = (curRecvAck + 1) % SEQ_SIZE;
		totalRecv++;
	}
	if (i != 0) {
		int j;
		for (j = 0; j < RECV_WIND_SIZE - i; j++) {
			inRecvCache[j] = inRecvCache[j + i];
			if (inRecvCache[j] == TRUE) {
				memcpy(recvBuffer[j], recvBuffer[j + i], PROTOCALSIZE);
			}
		}
		for (; j < RECV_WIND_SIZE; j++) {
			inRecvCache[j] = FALSE;
		}
	}
}

//判断是否发送ack
int IfSendAck(const unsigned char seq) {
	int step = seq - curRecvAck;
	step = step >= 0 ? step : step + SEQ_SIZE;
	if (step < RECV_WIND_SIZE) {
		return TRUE;
	}
	step = curRecvAck - seq;
	step = step >= 0 ? step : step + SEQ_SIZE;
	if (step < RECV_WIND_SIZE) {
		return TRUE;
	}
	return FALSE;
}

//超时重传处理函数
void timeHandler(unsigned char type) {
	switch (type) {
	case GBN_SEND:
		TimeCount[curSendAck]++;
		if (TimeCount[curSendAck] > 5) {
			printf("Timer out error.\n");
			int index;
			for (int i = 0; i < SEND_WIND_SIZE; ++i) {
				index = (i + curSendAck) % SEQ_SIZE;
				ack[index] = TRUE;
			}
			if (totalSeq >= SEND_WIND_SIZE) {
				totalSeq -= SEND_WIND_SIZE;
			}
			else {
				totalSeq = 0;
			}
			curSendSeq = curSendAck;
			TimeCount[curSendAck] = 0;
		}
		break;
	case SR_SEND:
		for (int i = 0; i < SEQ_SIZE; i++) {
			if (!ack[i]) {
				TimeCount[i]++;
				if (TimeCount[i] > 5) {
					printf("Timer out error.\n");
					ack[i] = TRUE;
					curSendSeq = curSendAck;
					totalSeq--;
					TimeCount[i] = 0;
				}
			}
		}
		break;
	}
}

//返回功能码
int func(char*buffer) {
	if (strcmp(buffer, "-quit") == 0) {
		return FUNC_QUIT;
	}
	else if (strcmp(buffer, "-testgbn") == 0) {
		return FUNC_TESTGBN;
	}
	else if (strcmp(buffer, "-testsr") == 0) {
		return FUNC_TESTSR;
	}
	else if (strcmp(buffer, "-testcs") == 0) {
		return FUNC_TESTCS;
	}
	else {
		return -1;
	}
}

//根据丢失率随机生成一个数字，判断是否丢失,丢失则返回TRUE，否则返回 FALSE
BOOL lossInLossRatio(float lossRatio) {
	int lossBound = (int)(lossRatio * 100);
	int r = rand() % 101;
	if (r <= lossBound) {
		return TRUE;
	}
	return FALSE;
}

int Test(struct sockaddr_in addrClient, struct Protocal* protocal, unsigned char type, int stage) {
	char buffer[BUFFER_SIZE];
	ZeroMemory(buffer, sizeof(buffer));
	char* data = NULL;
	int filelength;
	int recvSize;
	int length = sizeof(SOCKADDR);
	int waitCount = 0;
	BOOL b;
	unsigned char seq;//包的序列号
	while (TRUE)
	{
		switch (stage) {
		case 0://发送 205 阶段
			buffer[0] = 205;
			sendto(Server, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
			Sleep(100);
			stage = 1;
			break;
		case 1://等待接收 200 阶段，没有收到则计数器+1，超时则放弃此次“连接”，等待从第一步开始
			if ((data = malloc(DATA_SIZE)) == NULL) {
				return -1;
			}
			ZeroMemory(data, DATA_SIZE);
			if ((filelength = Readfile("test.txt", data, DATA_SIZE)) == -1) {
				return -1;
			}
			totalPacket = filelength / 1024 + 1;
			recvSize = recvfrom(Server, buffer, BUFFER_SIZE, 0, ((SOCKADDR*)&addrClient), &length);
			if (recvSize < 0) {
				++waitCount;
				if (waitCount > 20) {
					printf("Timeout error\n");
					return -1;
					break;
				}
				Sleep(500);
				continue;
			}
			else {
				if ((unsigned char)buffer[0] == 200) {
					printf("Begin a file transfer\n");
					printf("File size is %dB, each packet is 1024B and packet total num is % d\n", filelength, totalPacket);
					curSendSeq = 0;
					curSendAck = 0;
					totalSeq = 0;
					totalAck = 0;
					for (int i = 0; i < SEQ_SIZE; i++) {
						ack[i] = TRUE;
						TimeCount[i] = 0;
						inSendCache[i] = FALSE;
					}
					stage = 2;
				}
			}
			break;
		case 2://数据传输阶段
			if (!SendHandler(Server, protocal, &addrClient, data, buffer, type)) {
				free(data);
				printf("传输完成！\n");
				return 0;
			}
			recvSize = recvfrom(Server, buffer, BUFFER_SIZE, 0, ((SOCKADDR*)&addrClient), &length);
			if (recvSize < 0) {
				timeHandler(type);
			}
			else {
				RecvHandler(Server, protocal, &addrClient, buffer);
			}
			Sleep(500);
			break;
		case 3://等待握手阶段
			printf("%s\n", "Begin to test protocol, please don't abort the process");
			printf("The loss ratio of packet is %.2f\n", packetLossRatio);
			for (int i = 0; i < RECV_WIND_SIZE; i++) {
				inRecvCache[i] = FALSE;
			}
			if ((data = malloc(DATA_SIZE)) == NULL) {
				return -1;
			}
			ZeroMemory(data, DATA_SIZE);
			recvSize = recvfrom(Server, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&addrClient, &length);
			if ((unsigned char)buffer[0] == 205)
			{
				printf("Ready for file transmission\n");
				buffer[0] = 200;
				buffer[1] = '\0';
				sendto(Server, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
				stage = 4;
				totalRecv = 0;
				curRecvAck = 0;
			}
			break;
		case 4://等待接收数据阶段
			//随机法模拟包是否丢失
			recvSize = recvfrom(Server, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&addrClient, &length);
			if (recvSize < 0) {
				waitCount++;
				if (waitCount > 40) {
					printf("已超时，未收到结束报文\n");
					return 0;
				}
				Sleep(500);
			}
			else {
				waitCount = 0;
				ParseProtocalHeader(protocal, buffer);
				seq = protocal->seq;
				b = lossInLossRatio(packetLossRatio);
				if (b) {
					printf("The packet with a seq of %d loss\n", seq);
					continue;
				}
				//如果是期待的包，正确接收，正常确认即可
				if (!RecvHandler(Server, protocal, &addrClient, buffer)) {
					Writefile("recv.txt", data, totalRecv * PROTOCALSIZE);
					printf("传输完成!\n");
					free(data);
					return 0;
				}
				RemoveRecvBuffer(data);
				/*b = lossInLossRatio(ackLossRatio);
				if (b) {
					printf("The ack of %d loss\n", seq);
					continue;
				}*/
			}
			break;
		}
		Sleep(500);
	}
}

//处理待发送的协议报文
int SendHandler(SOCKET Server,struct Protocal*protocal,struct sockaddr_in* addrClient,char*data,char*buffer,unsigned char type) {
	if (totalAck == totalPacket) {
		PacketProtocal(protocal, SEND_END,255, "");
		CreateProtocalHeader(protocal, buffer);
		sendto(Server, buffer, BUFFER_SIZE, 0, (SOCKADDR*)addrClient, sizeof(SOCKADDR));
		return 0;
	}
	else if(seqIsAvailable()&&totalSeq<totalPacket){
		//发送给客户端的序列号从0开始
		if (inSendCache[curSendSeq] == FALSE) {
			PacketProtocal(protocal, type, curSendSeq, data + 1024 * totalSeq);
			CreateProtocalHeader(protocal, buffer);
			inSendCache[curSendSeq] = TRUE;
			memcpy(sendBuffer[curSendSeq], buffer,BUFFER_SIZE);
		}
		ack[curSendSeq] = FALSE;
		printf("send a packet with a seq of %d\n", curSendSeq);
		sendto(Server, sendBuffer[curSendSeq], BUFFER_SIZE, 0, (SOCKADDR*)addrClient, sizeof(SOCKADDR));
		curSendSeq=(curSendSeq+1)%SEQ_SIZE;
		totalSeq++;
		Sleep(500);
	}
	return 1;
}

//将收到的数据存入接收窗口
void SaveRecv(int seq, char* content) {
	int step = seq - curRecvAck;
	step = step >= 0 ? step : step + SEQ_SIZE;
	if (step < RECV_WIND_SIZE) {
		inRecvCache[step] = TRUE;
		memcpy(recvBuffer[step], content, PROTOCALSIZE);
	}
}

//处理收到的协议报文,若收到结束报文返回0
int RecvHandler(SOCKET Server, struct Protocal* protocal, struct sockaddr_in* addrClient, char* buffer) {
	ParseProtocalHeader(protocal, buffer);
	int length = sizeof(SOCKADDR);
	int ret;
	unsigned char index = protocal->seq;
	printf("recv a packet with a seq of %d\n", curSendAck);
	switch (protocal->func) {
	case SEND_END:
		return 0;
		break;
	case SR_SEND:
		if (IfSendAck(index)) {
			printf("send a ack of %d\n", index);
			SaveRecv(index, protocal->content);
			PacketProtocal(protocal, SR_ACK, index, "");
			CreateProtocalHeader(protocal, buffer);
			ret = sendto(Server, buffer, BUFFER_SIZE, 0, (SOCKADDR*)addrClient, length);
		}
		break;
	case GBN_SEND:
		if (IfSendAck(index)) {
			printf("send a ack of %d\n", index);
			SaveRecv(index, protocal->content);
			PacketProtocal(protocal, GBN_ACK, index, "");
			CreateProtocalHeader(protocal, buffer);
			ret = sendto(Server, buffer, BUFFER_SIZE, 0, (SOCKADDR*)addrClient, length);
		}
		break;
	case GBN_ACK:
		if (index == curSendAck) {
			TimeCount[curSendAck] = 0;
			ack[index] = TRUE;
			inSendCache[curSendAck] = FALSE;
			curSendAck=(curSendAck+1)%SEQ_SIZE;
			totalAck++;
		}
		break;
	case SR_ACK:
		if (IfinSendWind(protocal->seq)) {
			//SR协议
			ack[index] = TRUE;
			TimeCount[index] = 0;
			if (index == curSendAck) {
				inSendCache[index] = FALSE;
				int i;
				for (i = 1; i < SEND_WIND_SIZE; i++) {
					if (!inSendCache[curSendAck] || !ack[(curSendAck + i) % SEQ_SIZE]) {
						break;
					}
					else {
						inSendCache[(curSendAck + i) % SEQ_SIZE] = FALSE;
					}
				}
				totalAck += i;
				curSendAck = (curSendAck + i) % SEQ_SIZE;
			}
		}
		break;
	}
	return 1;
}

//服务器端提供服务
int Service() {
	SOCKADDR_IN addrClient; //客户端地址
	int length = sizeof(SOCKADDR);
	struct Protocal* protocal = malloc(sizeof(struct Protocal));
	char buffer[BUFFER_SIZE]; //数据发送接收缓冲区
	ZeroMemory(buffer, sizeof(buffer));
	int recvSize;
	while (TRUE) {
		//非阻塞接收，若没有收到数据，返回值为-1
		recvSize = recvfrom(Server, buffer, BUFFER_SIZE, 0, ((SOCKADDR*)&addrClient), &length);
		if (recvSize < 0) {
			Sleep(200);
			continue;
		}
		printf("recv from client: %s\n", buffer);
		switch (func(buffer)) {
		case FUNC_QUIT:
			strcpy_s(buffer, strlen("Good bye!") + 1, "Good bye!");
			break;
		case FUNC_TESTGBN:
			Test( addrClient, protocal,GBN_SEND,0);
			break;
		case FUNC_TESTSR:
			Test(addrClient, protocal, SR_SEND,0);
			break;
		case FUNC_TESTCS:
			Test(addrClient, protocal, GBN_SEND,3);
			break;
		}	
		sendto(Server, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, length); 
		Sleep(500);
	}
}