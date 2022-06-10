#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <ws2tcpip.h>
#include <string>
#include <list>
#include <vector>
#include <map>
#include <utility>
#include "Communication.h"

using namespace std;

#pragma comment(lib, "Ws2_32.lib")



//ФОРМАТ СООБЩЕНИЙ
//    SIZE(2 байта) ----DATA----     SIZE - размер сообщения в байтах
//

list<SOCKET> connectedClients;//лист сокетов подключенных клиентов
struct sockAddrLen {
	sockaddr addr;
	int len;
};
map<SOCKET, sockAddrLen> ClientsAddresses;//клиенские адреса в зависимости от созданного сокета

bool isActive;//активен ли сервер

void ServerMessageHandler(char* message, int size,SOCKET sock);//обработчки принятых сервером сообщений
int InitClientConnection(SOCKET sock);//запрос доп инфы для полного соединения с клиентом


int countLis = 6;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lCmdLine, int nCmdShow) {
	WSADATA wsaData;
	int iRes = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iRes != 0) {
		MessageBox(NULL, L"Error", L"Oops", MB_OK | MB_ICONERROR);
		return 1;
	}
	SOCKET listenSocket = INVALID_SOCKET;
	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET) {
		MessageBox(NULL, L"Невозможно создать сокет", L"Oops", MB_OK | MB_ICONERROR);
		return 1;
	}
	//создания статического адреса для сервера приложения
	sockaddr_in service;
	service.sin_family = AF_INET;
	service.sin_addr.s_addr = inet_addr(SERVERIP);//преобразования IPv4 строки в нужный формат
	service.sin_port = htons(SERVERPORT);

	// привязка адреса к серверу
	iRes = bind(listenSocket, (SOCKADDR*)&service, sizeof(service));
	if (iRes == SOCKET_ERROR) {
		MessageBox(NULL, L"Невозможно связать адрес сервера", L"Oops", MB_OK | MB_ICONERROR);
		closesocket(listenSocket);
		return 1;
	}
	iRes = listen(listenSocket, 5);
	if (iRes != 0) {
		MessageBox(NULL, L"Невозможно создать очередь на прием соединений", L"Oops", MB_OK | MB_ICONERROR);
		closesocket(listenSocket);
		return 1;
	}
	isActive = true;
	
	MessageBox(NULL, L"Сервер был успешно запущен", L"Ok", MB_OK | MB_ICONINFORMATION);
	vector<list<SOCKET>::iterator> socketsToDelete;
	while (isActive) {
		fd_set setOfSockets;
		static TIMEVAL wait_time;//максимальное время ожидания
		wait_time.tv_sec = 20;
		FD_ZERO(&setOfSockets);
		for (auto& x : connectedClients) {
			FD_SET(x, &setOfSockets);
		}
		FD_SET(listenSocket, &setOfSockets);
		select(0, &setOfSockets, NULL, NULL, &wait_time);			//принимаем сообщения от клиентов
		if (FD_ISSET(listenSocket, &setOfSockets)) {
			//принять новое соединение(создадим новый поток? - тогда продумать борьбу за буфер)
			InitClientConnection(listenSocket);
		}
		list<SOCKET>::iterator iter=connectedClients.begin(), iterEnd = connectedClients.end();
		socketsToDelete.clear();
		while (iter!=iterEnd) {
			if (FD_ISSET(*iter, &setOfSockets)) {//обычный прием сообщений
				int iRes=recv(*iter, RecieveBuffer, MAX_LEN, 0);
				if (iRes <= 0) {
					//соединение с клиентом разорвано на приемку(и в целом)
					shutdown(*iter, SD_BOTH);
					closesocket(*iter);
					socketsToDelete.push_back(iter);
				}
				else {
					//обработка сообщений от клиентов
					GetFullMessageFromNode(*iter, iRes,ServerMessageHandler);
				}
			}
			iter++;
		}
		//удалим старые сокеты
		for (auto& x : socketsToDelete) {
			ClientsAddresses.erase(*x);
			connectedClients.erase(x);
			//уведомить об этом других клиентов?
		}
		if (countLis == 0) isActive = false;
	}
	closesocket(listenSocket);
	WSACleanup();
	MessageBox(NULL, L"Сервер завершил свою работу", L"Error", MB_ICONWARNING | MB_OK);

	return 0;
}

//////////ИНФОРМАТИВНАЯ ЧАСТЬ ПРИЛОЖЕНИЯ

map<wstring, SOCKET> clientsNames;//имена клиентов и соответствующие им сокеты

void BroadMessage(char type, char* data, int size);//широковешательный ответ пользователям

void ServerMessageHandler(char* message, int size,SOCKET sock) {
	char type = *message;
	message++;
	wstring mesStr; SOCKET destSock;
	map<wstring, SOCKET>::iterator iter;
	switch (type) {
	case MESSAGE_DIALOGBOX:
		MessageBox(NULL, (WCHAR*)message, L"Hello", MB_OK);
		break;

	case MESSAGE_TOCONNECT://просьба соединения с другим клиентом
		mesStr = (wchar_t*)message;//имя щапрашиваемого клиента
		iter = clientsNames.find(mesStr);
		if (iter != clientsNames.end()) {
			SendMessageToNode((*iter).second, MESSAGE_OPENSOCKET, (char*)&sock, sizeof(SOCKET));//просьба открыть приемное соединение
		}
		else {
			mesStr = L"Клиент с таким именем не найден";
			SendMessageToNode(sock, MESSAGE_DIALOGBOX, (char*)&mesStr[0], (mesStr.size() + 1) * sizeof(wchar_t));
		}
		break;

	case MESSAGE_WAITFORCONNECTION:
		destSock = *(SOCKET*)message;//адрес запросившего узла
		message += sizeof(SOCKET);
		SendMessageToNode(destSock, MESSAGE_TOCONNECT, message, sizeof(sockaddr));
		break;
	}
}

void ServerMessageSender(SOCKET sock,char type) {//посылка основных сообщений клиенту
	//предполагаем, что размер данных влазит
	static char Buffer1[MAX_MESSAGE_SIZE];//КОЛЛИЗИЯ К БУФЕРУ ПРИ МНОГОПОТОЧНОСТИ
	int offset = 0;
	switch (type) {
	case MESSAGE_SHOWALLUSERS://отослать клиенту список всех пользователей
		char userCount = clientsNames.size();
		Buffer1[offset++] = userCount;
		for (auto& x : clientsNames) {
			wstring name = x.first;
			char sizeName = (name.size() + 1) * sizeof(wchar_t);
			Buffer1[offset++] = sizeName;
			copy((char*)&name[0], (char*)(&name[name.size() - 1]) + 4, Buffer1 + offset);
			offset += sizeName;
		}
		SendMessageToNode(sock, MESSAGE_SHOWALLUSERS, Buffer1, offset);
		break;
	}
}

int InitClientConnection(SOCKET listenSocket) {//запрос доп инфы для полного соединения с клиентом
	pair<SOCKET, sockAddrLen> pairSock;
	map<wstring, SOCKET>::iterator namesIter;
	wstring name;
	pairSock.second.len = sizeof(pairSock.second.addr);
	SOCKET sock = accept(listenSocket, &pairSock.second.addr, &pairSock.second.len);
	if (sock == INVALID_SOCKET) {
		MessageBox(NULL, L"Невозможно установить подключение с клиентом", L"Oops", MB_OK | MB_ICONERROR);
		return 1;
	}
	//получение имени от клиента
	bool isName = 1;
	while (isName) {
		SendMessageToNode(sock, MESSAGE_GIVENAME, NULL, 0);
		int iRecive = recv(sock, RecieveBuffer, MAX_LEN, 0);//блокируем поток, пока не получим ответа
		if (iRecive <= 0) {
			closesocket(sock);
			return 1;
		}
		else {
			GetFullMessageFromNode(sock, iRecive);
			//обработка пришедшего ответа от клиента
			char* message = MessageFromClient;
			char type = *message;//тип посланного пакета
			message++; messageSize--;
			switch (type) {
			case MESSAGE_GETNAME://пришедшее имя
				name = (wchar_t*)message;
				//проверим, было ли такое имя зарегистировано
				namesIter = clientsNames.find(name);
				if (namesIter == clientsNames.end()) {//если такого имени не было
					//регистрация имени
					pair<wstring, SOCKET> link;
					link.first = name; link.second = sock;
					clientsNames.insert(link);
					//уведомить о новом клиенте других клиентов
					BroadMessage(MESSAGE_ADDNEWUSER, (char*)&name[0], (name.size() + 1) * sizeof(wchar_t));
					ServerMessageSender(sock, MESSAGE_SHOWALLUSERS);
					isName = 0;
				}
				else {
					name = L"Имя занято другим пользователем";
					SendMessageToNode(sock, MESSAGE_DIALOGBOX, (char*)&name[0], (name.size() + 1) * sizeof(wchar_t));
				}
				break;
			}
		}
	}
	connectedClients.push_back(sock);//добавим сокет с клиентом в список
	pairSock.first = sock;
	ClientsAddresses.insert(pairSock);
	countLis--;
	return 0;
}


void BroadMessage(char type,char* data,int size) {
	list<SOCKET>::iterator iter=connectedClients.begin(),endIter=connectedClients.end();
	for (; iter != endIter; iter++) {
		SendMessageToNode(*iter, type, data, size);
	}
}

