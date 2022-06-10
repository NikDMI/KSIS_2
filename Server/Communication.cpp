#include "Communication.h"


const int MAX_LEN = 1000;
char RecieveBuffer[MAX_LEN];

char MessageFromClient[MAX_MESSAGE_SIZE+100];//буффер для сообщения
int messageSize;

#define sizeType uint16_t


void GetFullMessageFromNode(SOCKET socket, int size, messageHandler handler,int offset) {//получение полного сообщения от клиента
	char bufSize[sizeof(sizeType)];
	bool isMessage = true;
	while (isMessage) {//есть еще непроитанные сообщения
		messageSize = 0;//размер принятого сообщения
		sizeType rsize = -1;//размер переданного сообщения сооьщения
		if (size >= sizeof(sizeType)) {//если он был считан
			rsize = *(sizeType*)(RecieveBuffer + offset);
			offset += sizeof(sizeType);
			size -= sizeof(sizeType);
		}
		else {
			for (int i = 0; i < size; i++) {
				bufSize[i] = RecieveBuffer[offset + i];
			}
			int readSizeBytes = size;
			//принимаем следующий блок данных
			size = recv(socket, RecieveBuffer, MAX_LEN, 0);
			offset = sizeof(sizeType) - readSizeBytes;//сколько байт дочитать размер пакета
			for (int i = 0; i < offset; i++) {
				bufSize[readSizeBytes+i] = RecieveBuffer[i];
			}
			rsize = *(sizeType*)bufSize;
			size -= offset;
		}
		bool isRead = true;//читается ли очередное сообщенеи
		while (isRead) {
			if (rsize == size) {//сообщение полностью прочитано
				copy(RecieveBuffer + offset, RecieveBuffer + offset + rsize, MessageFromClient + messageSize);
				messageSize += rsize;
				//отправить сообщение на обработку
				if (handler != NULL) handler(MessageFromClient, messageSize, socket);
				isRead = false;
				isMessage = false;
			}
			else if (size > rsize) {//если было захвачено и следующее сообщение
				copy(RecieveBuffer + offset, RecieveBuffer + offset + rsize, MessageFromClient + messageSize);//!!!
				messageSize += rsize;
				//отправить сообщение на обработку
				if (handler != NULL) handler(MessageFromClient, messageSize, socket);
				isRead = false;
				//GetFullMessageFromNode(socket, size - rsize, handler, offset + rsize);
				size = size - rsize;
				offset = offset + rsize;
			}
			else {//если считано недостаточно байт
				copy(RecieveBuffer + offset, RecieveBuffer + offset + size, MessageFromClient + messageSize);
				messageSize += size;
				if (messageSize < 0 || messageSize>MAX_MESSAGE_SIZE+1000) {
					Sleep(100);
				}
				rsize -= size;
				size = recv(socket, RecieveBuffer, MAX_LEN, 0);
				offset = 0;
			}
		}
	}
}

void SendMessageToNode(SOCKET socketTo, char type, char* data, int size) {
	sizeType messSize = size + sizeof(char) + sizeof(sizeType);//общий размер посылаемого пакеда данных
	*(sizeType*)MessageFromClient = size+sizeof(char);//размер содержимого пакета без размера (чистое содержимое)
	*(MessageFromClient + sizeof(sizeType)) = type;//указание типа посылаемого пакета
	if (data!=NULL) copy(data, data + size, MessageFromClient + sizeof(sizeType) + sizeof(char));
	send(socketTo, MessageFromClient, messSize, 0);
}

void SendMessageToNodeUDP(SOCKET socketTo, sockaddr* addrTo,int toLen ,char type, char* data, int size) {
	sizeType messSize = size + sizeof(char);//общий размер посылаемого пакеда данных
	*(sizeType*)MessageFromClient = size + sizeof(char);//размер содержимого пакета без размера (чистое содержимое)
	*(MessageFromClient + sizeof(sizeType)) = type;//указание типа посылаемого пакета
	if (data != NULL) copy(data, data + size, MessageFromClient + sizeof(sizeType) + sizeof(char));
	int i=sendto(socketTo, MessageFromClient, messSize+sizeof(sizeType), 0, addrTo, toLen);
	if (i < 0) {
		i=WSAGetLastError();
	}
}

char* GetFullMessageFromNodeUDP(SOCKET socket, sockaddr& addrFrom, int& fromLen,messageHandler handler) {
	int count=recvfrom(socket, MessageFromClient, MAX_MESSAGE_SIZE+100, 0, &addrFrom, &fromLen);
	messageSize = *(sizeType*)(&MessageFromClient);
	return MessageFromClient + sizeof(sizeType);
}