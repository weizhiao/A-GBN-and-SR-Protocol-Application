#include <stdlib.h>
#include<stdio.h>
#include<Windows.h>
#include <time.h>

#pragma comment(lib,"ws2_32.lib")

#define FUNC_QUIT 1	//�˳�
#define FUNC_TESTGBN 2//����GBN
#define FUNC_TESTSR	3//����SR
#define FUNC_TESTCS	4//����˫��ͨ��
#define SR_SEND 0	//ʹ��SR�ķ��ͱ���
#define SR_ACK 1	//ʹ��SR��ȷ�ϱ���
#define GBN_SEND 2	//ʹ��GBN�ķ��ͱ���
#define GBN_ACK 3	//ʹ��GBN��ȷ�ϱ���
#define SEND_END 4	//���ͽ�������
#define SERVER_PORT 8080 //�������ݵĶ˿ں�
#define SERVER_IP "127.0.0.1" // �������� IP ��ַ
#define PROTOCALSIZE 1024
#define BUFFER_SIZE 1026
#define SEQ_SIZE 20
#define SEND_WIND_SIZE 10	//���ʹ��ڴ�СΪ 10
#define RECV_WIND_SIZE 1	//���մ��ڴ�СΪ 10
#define DATA_SIZE 1024*30 //�ļ���ȡ������ֽ�

struct Protocal {
	unsigned char func;
	unsigned char seq;
	char content[PROTOCALSIZE];
};

char sendBuffer[SEQ_SIZE][BUFFER_SIZE];//���ʹ���
int TimeCount[SEQ_SIZE];//��ʱ��
BOOL ack[SEQ_SIZE];//�յ� ack �������Ӧ 0~19 �� ack
BOOL inRecvCache[SEQ_SIZE];//�յ����ݶε����
BOOL inSendCache[SEQ_SIZE];//�Ƿ��ڷ��ʹ���
char recvBuffer[RECV_WIND_SIZE][PROTOCALSIZE];//���ջ�����
int curSendSeq;//��ǰ�������ݰ���seq
int curSendAck;//��ǰ�ȴ�ȷ�ϵ���С����
int curRecvAck;//��������С�����к�
int totalSeq;//���͵İ�����
int totalAck;//�յ���ack����
int totalPacket;//��Ҫ���͵İ�����
int totalRecv;//�ܹ����յ��İ�����
SOCKET Server;
struct sockaddr_in addrServer;
int length = sizeof(SOCKADDR);
float packetLossRatio;//������

//���������������׽���
void InitSocket();
//����Э��ṹ��
void PacketProtocal(struct Protocal* protocal, unsigned char func, unsigned char seq, char* content);
//����Э�鱨��
void CreateProtocalHeader(struct Protocal* protocal, char* Buffer);
//����Э�鱨��
void ParseProtocalHeader(struct Protocal* protocal, char* Buffer);
//�����׽��ֿ�
int LoadDLL();
//���ݶ�ʧ���������һ�����֣��ж��Ƿ�ʧ,��ʧ�򷵻�TRUE�����򷵻� FALSE
BOOL lossInLossRatio(float lossRatio);
//��������͵�Э�鱨��
int SendHandler(SOCKET Server, struct Protocal* protocal, struct sockaddr_in* addrClient, char* data, char* buffer, unsigned char type);
//���յ������ݴ�����մ���
void SaveRecv(int seq, char* content);
//���������������Ƴ�
void RemoveRecvBuffer(char* data);
//�����յ���Э�鱨��
int RecvHandler(SOCKET Server, struct Protocal* protocal, struct sockaddr_in* addrClient, char* buffer);
void printTips();
//���ع�����
int func(char* cmd);
//����
int Test(struct sockaddr_in addrClient, struct Protocal* protocal, unsigned char type, int stage);
//�ж�����Ƿ��ڷ��ʹ�����
int IfinSendWind(const unsigned char seq);
//�ж��Ƿ���ack
int IfSendAck(const unsigned char seq);
//��ǰ���к� curSeq �Ƿ����
int seqIsAvailable();
//�ͻ��˷���
void Client();
//��ȡ�ļ�
int Readfile(char* filename, char* data, int len);
//д�ļ�
int Writefile(char* filename, char* data, int len);

int main(int argc, char* argv[])
{
	printf("���ڼ����׽��ֿ�...\n");
	if (LoadDLL() == FALSE) {
		printf("�����׽��ֿ�ʧ��\n");
		return -1;
	}
	printf("���سɹ���\n");
	InitSocket();
	Client();
	//�ر��׽���
	closesocket(Server);
	WSACleanup();
	return 0;
}

//��ȡ�ļ�
int Readfile(char* filename, char* data, int len) {
	FILE* fp;
	if (fopen_s(&fp, filename, "r") != 0) {
		printf("�޷����ļ�%s\n", filename);
		return -1;
	}
	int current = 0;
	char ch;
	while ((ch = fgetc(fp)) != EOF) {
		if (len == current) {
			printf("�ڴ��С���㣬�޷���ȡ�����ļ�!\n");
			break;
		}
		data[current++] = ch;
	}
	fclose(fp);
	return current;
}

//д�ļ�
int Writefile(char* filename, char* data, int len) {
	FILE* fp;
	fopen_s(&fp, "recv.txt", "w");
	fwrite(data, 1, len, fp);
	fclose(fp);
}

//����Э��ṹ��
void PacketProtocal(struct Protocal* protocal, unsigned char func, unsigned char seq, char* content) {
	ZeroMemory(protocal, sizeof(struct Protocal));
	protocal->func = func;
	protocal->seq = seq;
	memcpy(protocal->content, content, PROTOCALSIZE);
}

//����Э�鱨��
void CreateProtocalHeader(struct Protocal* protocal, char* Buffer) {
	int current = 0;
	memcpy(Buffer, &protocal->func, 1);
	current++;
	memcpy(&Buffer[current++], &protocal->seq, 1);
	memcpy(&Buffer[current], protocal->content, PROTOCALSIZE);
}

//����Э�鱨��
void ParseProtocalHeader(struct Protocal* protocal, char* Buffer) {
	memcpy(&protocal->func, Buffer, 1);
	memcpy(&protocal->seq, &Buffer[1], 1);
	memcpy(protocal->content, &Buffer[2], PROTOCALSIZE);
}

//�����׽��ֿ�
int LoadDLL() {
	WORD wVersionRequested;
	WSADATA wsaData;
	//�׽��ּ���ʱ������ʾ
	int err;
	//�汾 2.2
	wVersionRequested = MAKEWORD(2, 2);
	//���� dll �ļ� Scoket ��
	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		//�Ҳ��� winsock.dll
		printf("���� winsock ʧ�ܣ��������Ϊ: %d\n", WSAGetLastError());
		return FALSE;
	}
	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
	{
		printf("�����ҵ���ȷ�� winsock �汾\n");
		WSACleanup();
		return FALSE;
	}
	return TRUE;
}

/****************************************************************/
/* -time �ӷ������˻�ȡ��ǰʱ��
-quit �˳��ͻ���
-testgbn [X] ���� GBN Э��ʵ�ֿɿ����ݴ���
[X] [0,1] ģ�����ݰ���ʧ�ĸ���
[Y] [0,1] ģ�� ACK ��ʧ�ĸ���
*/
/****************************************************************/
void printTips() {
	printf("*****************************************\n");
	printf("| -quit to exit client |\n");
	printf("| -testgbn             |\n");
	printf("| -testsr             |\n");
	printf("| -testcs             |\n");
	printf("*****************************************\n");
}

//���ݶ�ʧ���������һ�����֣��ж��Ƿ�ʧ,��ʧ�򷵻�TRUE�����򷵻� FALSE
BOOL lossInLossRatio(float lossRatio) {
	int lossBound = (int)(lossRatio * 100);
	int r = rand() % 101;
	if (r <= lossBound) {
		return TRUE;
	}
	return FALSE;
}

//���ع�����
int func(char* buffer) {
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

void InitSocket() {
	Server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	addrServer.sin_addr.s_addr = inet_addr("127.0.0.1");
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(SERVER_PORT);
	int iMode = 1;
	ioctlsocket(Server, FIONBIO, (u_long FAR*) & iMode);//����������
}

//��ǰ���к� curSeq �Ƿ����
int seqIsAvailable() {
	int step;
	step = curSendSeq - curSendAck;
	step = step >= 0 ? step : step + SEQ_SIZE;
	//���к��Ƿ��ڵ�ǰ���ʹ���֮��
	if (step >= SEND_WIND_SIZE) {
		return FALSE;
	}
	if (ack[curSendSeq]) {
		return TRUE;
	}
	return FALSE;
}

//��ʱ�ش�������
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
				}
			}
		}
		break;
	}
}

//���յ������ݴ�����մ���
void SaveRecv(int seq,char*content) {
	int step = seq - curRecvAck;
	step = step >= 0 ? step : step + SEQ_SIZE;
	if (step < RECV_WIND_SIZE) {
		inRecvCache[step] = TRUE;
		memcpy(recvBuffer[step], content, PROTOCALSIZE);
	}
}

//��������͵�Э�鱨��
int SendHandler(SOCKET Server, struct Protocal* protocal, struct sockaddr_in* addrClient, char* data, char* buffer, unsigned char type) {
	if (totalAck == totalPacket) {
		PacketProtocal(protocal, SEND_END, 255, "");
		CreateProtocalHeader(protocal, buffer);
		sendto(Server, buffer, BUFFER_SIZE, 0, (SOCKADDR*)addrClient, sizeof(SOCKADDR));
		return 0;
	}
	else if (seqIsAvailable() && totalSeq < totalPacket) {
		//���͸��ͻ��˵����кŴ�0��ʼ
		if (inSendCache[curSendSeq] == FALSE) {
			PacketProtocal(protocal, type, curSendSeq, data + 1024 * totalSeq);
			CreateProtocalHeader(protocal, buffer);
			inSendCache[curSendSeq] = TRUE;
			memcpy(sendBuffer[curSendSeq], buffer, BUFFER_SIZE);
		}
		ack[curSendSeq] = FALSE;
		printf("send a packet with a seq of %d\n", curSendSeq);
		sendto(Server, sendBuffer[curSendSeq], BUFFER_SIZE, 0, (SOCKADDR*)addrClient, sizeof(SOCKADDR));
		curSendSeq = (curSendSeq + 1) % SEQ_SIZE;
		totalSeq++;
		Sleep(500);
	}
	return 1;
}

//�����յ���Э�鱨��,���յ��������ķ���0
int RecvHandler(SOCKET Server, struct Protocal* protocal, struct sockaddr_in* addrClient, char* buffer) {
	ParseProtocalHeader(protocal, buffer);
	int length = sizeof(SOCKADDR);
	int ret;
	unsigned char index = protocal->seq;
	printf("recv a packet with a seq of %d\n", index);
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
			printf("send a ack of %d\n", curRecvAck);
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
			curSendAck = (curSendAck + 1) % SEQ_SIZE;
			totalAck++;
		}
		break;
	case SR_ACK:
		if (IfinSendWind(protocal->seq)) {
			//SRЭ��
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

//�ж�����Ƿ��ڷ��ʹ�����
int IfinSendWind(const unsigned char seq) {
	int step;
	step = seq - curSendAck;
	step = step >= 0 ? step : step + SEQ_SIZE;
	if (step >= SEND_WIND_SIZE) {
		return FALSE;
	}
	return TRUE;
}

//���������������Ƴ�
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

//�ж��Ƿ���ack
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

int Test(struct sockaddr_in addrClient, struct Protocal* protocal, unsigned char type, int stage) {
	char buffer[BUFFER_SIZE];
	ZeroMemory(buffer, sizeof(buffer));
	char* data = NULL;
	int filelength;
	int recvSize;
	int length = sizeof(SOCKADDR);
	int waitCount = 0;
	BOOL b;
	unsigned char seq;//�������к�
	while (TRUE)
	{
		switch (stage) {
		case 0://���� 205 �׶�
			buffer[0] = 205;
			sendto(Server, buffer, strlen(buffer) + 1, 0, (SOCKADDR*)&addrClient, sizeof(SOCKADDR));
			Sleep(100);
			stage = 1;
			break;
		case 1://�ȴ����� 200 �׶Σ�û���յ��������+1����ʱ������˴Ρ����ӡ����ȴ��ӵ�һ����ʼ
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
		case 2://���ݴ���׶�
			if (!SendHandler(Server, protocal, &addrClient, data, buffer, type)) {
				free(data);
				printf("������ɣ�\n");
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
		case 3://�ȴ����ֽ׶�
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
		case 4://�ȴ��������ݽ׶�
			//�����ģ����Ƿ�ʧ
			recvSize = recvfrom(Server, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&addrClient, &length);
			if (recvSize < 0) {
				waitCount++;
				if (waitCount > 40) {
					printf("�ѳ�ʱ��δ�յ���������\n");
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
				//������ڴ��İ�����ȷ���գ�����ȷ�ϼ���
				if (!RecvHandler(Server, protocal, &addrClient, buffer)) {
					Writefile("recv.txt", data, totalRecv * PROTOCALSIZE);
					printf("�������!\n");
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

//�ͻ��˷���
void Client() {
	char buffer[BUFFER_SIZE];
	ZeroMemory(buffer, sizeof(buffer));
	struct Protocal* protocal = malloc(sizeof(struct Protocal));
	printTips();
	int ret;
	char cmd[128];
	packetLossRatio = 0.1; //Ĭ�ϰ���ʧ�� 0.3
	//��ʱ����Ϊ������ӣ�����ѭ����������
	srand((unsigned)time(NULL));
	while (TRUE) {
		scanf_s("%s", cmd, 128);
		sendto(Server, cmd, sizeof(cmd), 0, (SOCKADDR*)&addrServer, sizeof(SOCKADDR));
		switch (func(cmd)) {
		case FUNC_QUIT:
			break;
		case FUNC_TESTGBN:
		case FUNC_TESTSR:
			Test(addrServer, protocal, GBN_SEND, 3);
			break;
		case FUNC_TESTCS:
			Test(addrServer, protocal, SR_SEND, 0);
			break;
		}
		ret = recvfrom(Server, buffer, BUFFER_SIZE, 0, (SOCKADDR*)&addrServer, &length);
		printf("%s\n", buffer);
		if (!strcmp(buffer, "Good bye!")) {
			break;
		}
		printTips();
	}
}