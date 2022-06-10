#include "Communication.h"


const int MAX_LEN = 1000;
char RecieveBuffer[MAX_LEN];

char MessageFromClient[MAX_MESSAGE_SIZE+100];//������ ��� ���������
int messageSize;

#define sizeType uint16_t


void GetFullMessageFromNode(SOCKET socket, int size, messageHandler handler,int offset) {//��������� ������� ��������� �� �������
	char bufSize[sizeof(sizeType)];
	bool isMessage = true;
	while (isMessage) {//���� ��� ������������ ���������
		messageSize = 0;//������ ��������� ���������
		sizeType rsize = -1;//������ ����������� ��������� ���������
		if (size >= sizeof(sizeType)) {//���� �� ��� ������
			rsize = *(sizeType*)(RecieveBuffer + offset);
			offset += sizeof(sizeType);
			size -= sizeof(sizeType);
		}
		else {
			for (int i = 0; i < size; i++) {
				bufSize[i] = RecieveBuffer[offset + i];
			}
			int readSizeBytes = size;
			//��������� ��������� ���� ������
			size = recv(socket, RecieveBuffer, MAX_LEN, 0);
			offset = sizeof(sizeType) - readSizeBytes;//������� ���� �������� ������ ������
			for (int i = 0; i < offset; i++) {
				bufSize[readSizeBytes+i] = RecieveBuffer[i];
			}
			rsize = *(sizeType*)bufSize;
			size -= offset;
		}
		bool isRead = true;//�������� �� ��������� ���������
		while (isRead) {
			if (rsize == size) {//��������� ��������� ���������
				copy(RecieveBuffer + offset, RecieveBuffer + offset + rsize, MessageFromClient + messageSize);
				messageSize += rsize;
				//��������� ��������� �� ���������
				if (handler != NULL) handler(MessageFromClient, messageSize, socket);
				isRead = false;
				isMessage = false;
			}
			else if (size > rsize) {//���� ���� ��������� � ��������� ���������
				copy(RecieveBuffer + offset, RecieveBuffer + offset + rsize, MessageFromClient + messageSize);//!!!
				messageSize += rsize;
				//��������� ��������� �� ���������
				if (handler != NULL) handler(MessageFromClient, messageSize, socket);
				isRead = false;
				//GetFullMessageFromNode(socket, size - rsize, handler, offset + rsize);
				size = size - rsize;
				offset = offset + rsize;
			}
			else {//���� ������� ������������ ����
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
	sizeType messSize = size + sizeof(char) + sizeof(sizeType);//����� ������ ����������� ������ ������
	*(sizeType*)MessageFromClient = size+sizeof(char);//������ ����������� ������ ��� ������� (������ ����������)
	*(MessageFromClient + sizeof(sizeType)) = type;//�������� ���� ����������� ������
	if (data!=NULL) copy(data, data + size, MessageFromClient + sizeof(sizeType) + sizeof(char));
	send(socketTo, MessageFromClient, messSize, 0);
}

void SendMessageToNodeUDP(SOCKET socketTo, sockaddr* addrTo,int toLen ,char type, char* data, int size) {
	sizeType messSize = size + sizeof(char);//����� ������ ����������� ������ ������
	*(sizeType*)MessageFromClient = size + sizeof(char);//������ ����������� ������ ��� ������� (������ ����������)
	*(MessageFromClient + sizeof(sizeType)) = type;//�������� ���� ����������� ������
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