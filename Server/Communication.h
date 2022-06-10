#pragma once
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <inttypes.h>
#include <algorithm>

using namespace std;

#define SERVERPORT 3300
#define SERVERPORTSTR "3300"
#define SERVERIP "192.168.227.1"
#define MAX_MESSAGE_SIZE 65000

//типы сообщений между клиент-сервером
#define MESSAGE_DIALOGBOX 1
#define MESSAGE_GETNAME 2
#define MESSAGE_GIVENAME 3
#define MESSAGE_ADDNEWUSER 4
#define MESSAGE_SHOWALLUSERS 5
#define MESSAGE_TOCONNECT 6
#define MESSAGE_OPENSOCKET 7
#define MESSAGE_WAITFORCONNECTION 8
#define MESSAGE_SHOWFILES 9
#define MESSAGE_GETNEXTFILE 10
#define MESSAGE_GETFILETCP 11
#define MESSAGE_GETFILEUDP 12
#define MESSAGE_SETUDP 12
#define MESSAGE_OPENUDPFIREWALL 13
#define MESSAGE_UDPTRANSMISSION 14


//флаги отправки пакетов
#define lastPack 0x01

//для UDP
#define MAX_PACK_COUNT 1
const long long maxWait = 200000;//20 ms duration

//формирует и посылает сообщение в пределах максимального размера
void SendMessageToNode(SOCKET socket, char type, char* data, int size);
void SendMessageToNodeUDP(SOCKET socket,sockaddr* addrTo,int toLen, char type, char* data, int size);

extern const int MAX_LEN;
extern char RecieveBuffer[];

extern char MessageFromClient[MAX_MESSAGE_SIZE+100];//буффер для сообщения
extern int messageSize;

using messageHandler = void (*)(char* message,int size,SOCKET sock);

void GetFullMessageFromNode(SOCKET socket, int size, messageHandler handler=NULL,int offset = 0);
char* GetFullMessageFromNodeUDP(SOCKET socket,sockaddr& addrFrom, int& fromLen,messageHandler handler = NULL);