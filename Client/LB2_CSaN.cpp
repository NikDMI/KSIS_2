#include <WinSock2.h>
#include <ws2tcpip.h>
#include <string>
#include "../Server/Communication.h"
#include "../Framework_2903/WindowFramework.h"
#include <shlobj_core.h>
#include <fstream>
#include <chrono>
//#include <wrl.h>


using namespace std;

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "../Framework_2903/StaticLib1.lib")

wstring clientName;//имя компьютера, которое выбрал пользователь

bool isActive = 0;//активен ли клиентское соединение
SOCKET sock;

void CommunicateWithServer(SOCKET sock);
DWORD WINAPI GDIThread(LPVOID data);

WindowClass* mainWindow;
WindowClass* getNameDialog;
WindowClass* searchWindowMain;

bool isGDILoaded = 0;
enum class GDIState {MainMenu,GetNameDialog,Search};//состояния gdi
GDIState gdiState;
void SetGDIState(GDIState state);
void ResetNames();//перерисовывает список с именами

wstring rootDirectory = L"D://";

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lCmdLine, int nCmdShow) {
	int i=system("ls");
	WSADATA wsaData;
	int iRes = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iRes != 0) {
		MessageBox(NULL, L"Error", L"Oops", MB_OK | MB_ICONERROR);
		return 1;
	}
	addrinfo info, * result;
	sockaddr sa;
	ZeroMemory(&info, sizeof(addrinfo));
	info.ai_family = AF_UNSPEC;
	info.ai_socktype = SOCK_STREAM;
	info.ai_protocol = IPPROTO_TCP;
	info.ai_canonname = NULL;
	info.ai_next = NULL;
	iRes = getaddrinfo(SERVERIP,SERVERPORTSTR , &info, &result);
	if (iRes != 0) {
		MessageBox(NULL, L"Невозможно найти действующий адрес сервера", L"Oops", MB_OK | MB_ICONERROR);
		return 1;
	}
	
	SOCKET Socket = INVALID_SOCKET;
	Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (Socket == INVALID_SOCKET) {
		MessageBox(NULL, L"Сокет клиента не может быть создан", L"Oops", MB_OK | MB_ICONERROR);
		return 1;
	}
	iRes = connect(Socket, result->ai_addr, result->ai_addrlen);
	if (iRes == SOCKET_ERROR) {
		closesocket(Socket);
		MessageBox(NULL, L"Не можем подключиться к серверу", L"Oops", MB_OK | MB_ICONERROR);
	}
	isActive = 1;
	sock = Socket;
	HANDLE gdiThread=CreateThread(NULL, 0, GDIThread, NULL, 0, NULL);
	if (gdiThread != NULL) {
		while (!isGDILoaded) {
			Sleep(100);
		}//ждем загрузки гуи()
		CommunicateWithServer(Socket);
		WaitForSingleObject(gdiThread, INFINITE);
	}
	else isActive = 0;
	shutdown(Socket, SD_BOTH);
	closesocket(Socket);
	WSACleanup();
	return 0;
}

HANDLE getNameEvent;

vector<wstring> clientNames;//имена клиентов, которые доступны

//поток работы с другим клиентом
DWORD WINAPI CreateNewConnectionAsSender(LPVOID lpdata);
DWORD WINAPI CreateNewConnectionAsReciever(LPVOID lpdata);
HANDLE connectEvent; sockaddr currAddr;//для передачи инфы другому хосту, куда подключаться



///////////ВЗАИМОДЕЙСТВИЕ С СЕРВЕРОМ

void ProcessMessage(char* message,int sizeMessage,SOCKET sock) {//обработка сообщений от сервера
	static char data[1000]; 
	wstring strMess; SOCKET connectSocket;
	int size;
	char type = *message;//тип посланного пакета
	message++;
	switch (type) {
	case MESSAGE_GIVENAME://запрос на отправку своего имени(первый вход)
		//вызов диалога получения имени
		getNameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		SetGDIState(GDIState::GetNameDialog);
		WaitForSingleObject(getNameEvent, INFINITE);
		CloseHandle(getNameEvent);

		copy((char*)&clientName[0], (char*)&clientName[clientName.size() - 1] + 4, data);
		size = (clientName.size() + 1) * sizeof(wchar_t);
		SendMessageToNode(sock, MESSAGE_GETNAME, data, size);
		getNameDialog->HideWindow();
		SetGDIState(GDIState::MainMenu);
		break;

	case MESSAGE_DIALOGBOX://запрос на вывод уведомления пользователю
		strMess = (wchar_t*)message;
		MessageBox(NULL, &strMess[0], L"Message", MB_OK);
		break;

	case MESSAGE_ADDNEWUSER://появился в списке новый пользователь
		strMess = (wchar_t*)message;
		clientNames.push_back(strMess);
		ResetNames();
		break;

	case MESSAGE_SHOWALLUSERS://массив имен всех подключенных пользователей
		size = *message;//кол-во пользователей
		message++;
		clientNames.clear();
		for (int i = 0; i < size; i++) {
			char sizeName = *(message++);
			wstring name = (wchar_t*)message;
			message += sizeName;
			if (name != clientName) {
				clientNames.push_back(name);
			}
		}
		ResetNames();
		break;

	case MESSAGE_OPENSOCKET://просьба открыть новое соединение
		connectSocket = *(SOCKET*)message;
		connectEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		ZeroMemory(&currAddr, sizeof(sockaddr));
		CreateThread(NULL, 0, CreateNewConnectionAsSender, NULL, 0, NULL);
		WaitForSingleObject(connectEvent, INFINITE);
		((SOCKET*)data)[0] = connectSocket;
		((sockaddr*)(data + sizeof(SOCKET)))[0] = currAddr;
		SendMessageToNode(sock, MESSAGE_WAITFORCONNECTION, (char*)data, sizeof(sockaddr)+sizeof(SOCKET));
		break;

	case MESSAGE_TOCONNECT://другой хост открыл для нас очередь
		currAddr = *(sockaddr*)message;//адрес, к которому надо подключиться
		SetGDIState(GDIState::Search);
		CreateThread(NULL, 0, CreateNewConnectionAsReciever, NULL, 0, NULL);
		break;
	}
}

void CommunicateWithServer(SOCKET sock) {
	while (isActive) {
		int iRecive = recv(sock, RecieveBuffer, MAX_LEN, 0);
		if (iRecive <= 0) {
			isActive = 0;
		}
		else {
			GetFullMessageFromNode(sock, iRecive,ProcessMessage);
		}
	}
}

////////////// ВЗАИМОДЕЙСТВИЕ С ДРУГИМ КЛИЕНТОМ
/////////////ОТПРАВИТЕЛЬ

enum fileType { file, dir=FILE_ATTRIBUTE_DIRECTORY };
struct fileInfo {
	wstring fileName;
	int32_t type;
};

void messageHandlerSender(char* message, int sizeMessage, SOCKET sock);//обработчик сообщений к отправителю
vector<fileInfo> GetAllFiles(wstring dir);
void SendMessageToReciever(char type, char* message, int size, SOCKET sock);

map<SOCKET, wstring> nodesDirectories;//директории, в которых сейчас подключенные узлы


DWORD WINAPI CreateNewConnectionAsSender(LPVOID lpdata) {
	wstring currentDirectory = rootDirectory;//директория, в которой ищут файлы
	sockaddr name; int len = sizeof(name);
	int iRes = getsockname(sock, &name, &len);//получение адреса и порта, на котором сидит клиент
	if (iRes != 0) {
		SetEvent(connectEvent);
		return 1;
	}
	SOCKET listenSocket = INVALID_SOCKET;
	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	iRes = bind(listenSocket, &name, len);
	if (iRes == SOCKET_ERROR) {
		closesocket(listenSocket);
		SetEvent(connectEvent);
		return 1;
	}
	iRes=listen(listenSocket, 1);
	if (iRes != 0) {
		SetEvent(connectEvent);
		closesocket(listenSocket);
		return 1;
	}
	currAddr = name;
	SetEvent(connectEvent);
	SOCKET hostSocket=accept(listenSocket, NULL, NULL);
	if (hostSocket == INVALID_SOCKET) {
		closesocket(listenSocket);
		return 1;
	}
	closesocket(listenSocket);
	//работа с принимающей стороной
	pair<SOCKET, wstring> dirPair;
	dirPair.first = hostSocket; dirPair.second = currentDirectory;
	nodesDirectories.insert(dirPair);
	vector<fileInfo>files = GetAllFiles(currentDirectory);
	SendMessageToReciever(MESSAGE_SHOWFILES, (char*)&files, 0, hostSocket);
	bool isWork = 1;
	while (isWork) {
		int iRec = recv(hostSocket, RecieveBuffer, MAX_LEN, 0);//С РАЗНЫХ ПОТОКОВ ВОЗМОЖЕТ КОНФЛИКТ С БУФЕРОМ(надо делать критические секции)
		if (iRec > 0) {
			GetFullMessageFromNode(hostSocket, iRec,messageHandlerSender);
		}
		else {
			isWork = 0;
		}
	}
	nodesDirectories.erase(hostSocket);
	shutdown(hostSocket, SD_BOTH);
	closesocket(hostSocket);
	return 0;
}

void messageHandlerSender(char* message, int sizeMessage, SOCKET sock) {
	static char messageBuffer[MAX_MESSAGE_SIZE];
	wstring dir,fileName;
	map<SOCKET, wstring>::iterator iter;
	vector<fileInfo> files;
	static ifstream inputFile;
	static SOCKET udpSocket;
	static sockaddr recieverAddr; static int32_t recieverAddrLen;
	static char packBuffer[MAX_PACK_COUNT * MAX_MESSAGE_SIZE];//буфер для отправляющих пакетов
	int offPack;//на каком смещении в буфере младший пакет

	char type = *message;
	message++;
	iter = nodesDirectories.find(sock);
	switch (type) {
	case MESSAGE_GETNEXTFILE://пользователь хочет получить инфо о новой директории
		dir = (wchar_t*)message;//хапрашиваемая директория
		if (dir == L"..") {
			wstring& str = (*iter).second;
			str.erase(str.end() - 2, str.end());
			int off = str.size() - 1;
			while (str[off] != L'\\') {
				str.pop_back();
				off--;
			}
		}
		else {
			(*iter).second += dir+L"\\";
		}
		files = GetAllFiles((*iter).second);
		if (files.size()) {
			SendMessageToReciever(MESSAGE_SHOWFILES, (char*)&files, 0, sock);
		}
		else {
			wstring mess = L"Ошибка поиска файлов";
			SendMessageToNode(sock, MESSAGE_DIALOGBOX, (char*)&mess[0], (mess.size() + 1) * sizeof(wchar_t));
			shutdown(sock, SD_BOTH);
		}
		break;

	case MESSAGE_GETFILETCP://запрос на передачу файла
		fileName = (wchar_t*)message;
		inputFile.open((*iter).second+fileName, ios::in | ios::beg | ios::binary);
		if (inputFile) {
			do {
				inputFile.read(&messageBuffer[1], MAX_MESSAGE_SIZE-1);
				int readBytes = inputFile.gcount();
				char flag=0;
				if (!inputFile.good()) {
					flag |= lastPack;
				}
				messageBuffer[0] = flag;
				SendMessageToNode(sock, MESSAGE_GETFILETCP, messageBuffer, readBytes+1);
			} while (inputFile);
			inputFile.close();
		}
		else {
			wstring mess = L"Ошибка. Не могу открыть файл";
			SendMessageToNode(sock, MESSAGE_DIALOGBOX, (char*)&mess[0], (mess.size() + 1) * sizeof(wchar_t));
			shutdown(sock, SD_BOTH);
		}
		break;

	case MESSAGE_GETFILEUDP://запрос на передачу файла
		fileName = (wchar_t*)message;
		message += (fileName.size() + 1) * sizeof(wchar_t);
		recieverAddrLen = *((int32_t*)message);
		message += sizeof(int32_t);
		copy(message, message + recieverAddrLen, (char*)&recieverAddr);
		inputFile.open((*iter).second + fileName, ios::in | ios::beg | ios::binary);
		if (inputFile) {
			//открыть UDP соединение
			udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (udpSocket == INVALID_SOCKET) {
				inputFile.close();
				wstring mess = L"Ошибка. Не могу открыть UDP соединение";
				SendMessageToNode(sock, MESSAGE_DIALOGBOX, (char*)&mess[0], (mess.size() + 1) * sizeof(wchar_t));
				shutdown(sock, SD_BOTH);
				break;
			}
			sockaddr hostAddr; int addrLen = sizeof(hostAddr);
			if (getsockname(sock, &hostAddr, &addrLen) == 0) {
				if (bind(udpSocket, &hostAddr, addrLen) != 0) {
					inputFile.close();
					closesocket(udpSocket);
					wstring mess = L"Ошибка. Не могу открыть UDP соединение";
					SendMessageToNode(sock, MESSAGE_DIALOGBOX, (char*)&mess[0], (mess.size() + 1) * sizeof(wchar_t));
					shutdown(sock, SD_BOTH);
					break;
				}
			}
			else {
				inputFile.close();
				closesocket(udpSocket);
				wstring mess = L"Ошибка. Не могу открыть UDP соединение";
				SendMessageToNode(sock, MESSAGE_DIALOGBOX, (char*)&mess[0], (mess.size() + 1) * sizeof(wchar_t));
				shutdown(sock, SD_BOTH);
			}
			//открыть брандмауэр на последующий прием от этого адреса
			SendMessageToNodeUDP(udpSocket, &recieverAddr, recieverAddrLen, MESSAGE_OPENUDPFIREWALL,nullptr,0);
			int32_t len32 = addrLen;
			((int32_t*)(&messageBuffer[0]))[0] = len32;
			copy((char*)&hostAddr, (char*)&hostAddr + len32, messageBuffer+sizeof(len32));
			SendMessageToNode(sock, MESSAGE_SETUDP, messageBuffer, sizeof(len32)+len32);
		}
		else {
			wstring mess = L"Ошибка. Не могу открыть файл";
			SendMessageToNode(sock, MESSAGE_DIALOGBOX, (char*)&mess[0], (mess.size() + 1) * sizeof(wchar_t));
			shutdown(sock, SD_BOTH);
		}
		break;

	case MESSAGE_UDPTRANSMISSION://начать передачу файла по udp
		offPack = 0; int32_t nextPack = 0;
		int32_t lastProofPacket = 0;//последний подтсвержденный пакет(слудующий за ним)
		ofstream ftest;
		//ftest.open(L"Test", ios::out | ios::binary|ios::trunc);
		do {
			int dataMaxSize = MAX_MESSAGE_SIZE - 1 - sizeof(int32_t);
			inputFile.read(&messageBuffer[1+sizeof(int32_t)], dataMaxSize);
			int readBytes = inputFile.gcount();
			char flag = 0;
			if (!inputFile.good()) {
				flag |= lastPack;
			}
			//ftest.write(&messageBuffer[1 + sizeof(int32_t)], readBytes);
			messageBuffer[0] = flag;
			*(int32_t*)(&messageBuffer[1]) = nextPack;
			copy(&messageBuffer[0], &messageBuffer[0] + readBytes+sizeof(int32_t)+1, &packBuffer[((offPack+nextPack-lastProofPacket)%MAX_PACK_COUNT)*MAX_MESSAGE_SIZE]);
			
			nextPack++;
			SendMessageToNodeUDP(udpSocket, &recieverAddr, recieverAddrLen, MESSAGE_UDPTRANSMISSION, messageBuffer, readBytes + 1+sizeof(int32_t));
			while (nextPack - lastProofPacket == MAX_PACK_COUNT) {//забили буфер полностью, ждем подтверждения
				char* mess = GetFullMessageFromNodeUDP(udpSocket, recieverAddr, recieverAddrLen);
				char type = mess[0]; mess++;
				if (type == MESSAGE_OPENUDPFIREWALL) {
					int32_t pack = *(int32_t*)mess;//подствержденный пакет клиентом
					if (lastProofPacket<pack) {
						offPack = (offPack + pack - lastProofPacket) % MAX_PACK_COUNT;//сдвиг на кол-во подтвержденных пакетов
						lastProofPacket = pack;
					}
					else {//где-то пакет потерялся
							//отправить пакеты заново
						for (int i = 0; i < MAX_PACK_COUNT; i++) {
							int off = (offPack + i) % MAX_PACK_COUNT;//смещение в окне
							if (!packBuffer[off * MAX_MESSAGE_SIZE]) {//если это не последний пакет(проверяем флаг)
								SendMessageToNodeUDP(udpSocket, &recieverAddr, recieverAddrLen, MESSAGE_UDPTRANSMISSION, &packBuffer[off * MAX_MESSAGE_SIZE], MAX_MESSAGE_SIZE);
							}
							else {
								//размер с последнего чтения
								SendMessageToNodeUDP(udpSocket, &recieverAddr, recieverAddrLen, MESSAGE_UDPTRANSMISSION, &packBuffer[off * MAX_MESSAGE_SIZE], readBytes + 1 + sizeof(int32_t));
							}
						}
					}
				}
			}
			if (flag && lastProofPacket == nextPack) break;//все передали
		} while (inputFile);
		inputFile.close();
		//ftest.close();
		closesocket(udpSocket);
		break;
	}
}

void SendMessageToReciever(char type, char* message, int size, SOCKET sock) {
	vector<fileInfo>* files; int32_t s; int off;
	static char buf[MAX_MESSAGE_SIZE];
	switch (type) {
	case MESSAGE_SHOWFILES://отправить ему файлы директории
		files = (vector<fileInfo>*)message;
		s = files->size();
		((int32_t*)buf)[0] = s;
		off = sizeof(s);
		for (int i = 0; i < s; i++) {
			wstring name = (*files)[i].fileName;
			int strSize = (name.size() + 1) * sizeof(wchar_t);
			copy((char*)&name[0], (char*)&name[name.size()-1]+4, &buf[off]);
			off += strSize;
			((int32_t*)(&buf[off]))[0] = (*files)[i].type;
			off += sizeof(int32_t);
		}
		size = off;
		SendMessageToNode(sock, type, buf, size);
		break;
	}
}

vector<fileInfo> GetAllFiles(wstring dir) {
	dir += L"\\*";
	vector<fileInfo> files;
	WIN32_FIND_DATA fd;
	HANDLE find=FindFirstFile(&dir[0], &fd);
	if (find != INVALID_HANDLE_VALUE) {
		fileInfo info;
		
		bool flag = 1;
		while (flag) {
			info.fileName = fd.cFileName;
			info.type = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
			files.push_back(info);
			flag=FindNextFile(find, &fd);
		}
	}
	FindClose(find);
	return files;
}


/////////ПОЛУЧАТЕЛЬ

void messageHandlerReciever(char* message, int sizeMessage, SOCKET sock);//обработчик сообщений к получателю
vector<fileInfo> filesReciever;
SOCKET senderSocket;
sockaddr recieverAddr; int lenRecAddr = sizeof(sockaddr);
SOCKET recieverUDP;

ofstream outputFile;

DWORD WINAPI CreateNewConnectionAsReciever(LPVOID lpdata) {//поток как принимающая сторона
	sockaddr senderAddr = currAddr;
	SOCKET sock = INVALID_SOCKET;
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET) {
		return 1;
	}
	int iRes=connect(sock, &senderAddr, sizeof(sockaddr));
	if (iRes == SOCKET_ERROR) {
		closesocket(sock);
		return 1;
	}
	senderSocket = sock;
	//работа с отправляющей стороной
	bool isWork = 1;
	while (isWork) {
		int iRec = recv(sock, RecieveBuffer, MAX_LEN, 0);//С РАЗНЫХ ПОТОКОВ ВОЗМОЖЕТ КОНФЛИКТ С БУФЕРОМ(надо делать критические секции)
		if (iRec > 0) {
			GetFullMessageFromNode(sock, iRec, messageHandlerReciever);
		}
		else {
			isWork = 0;
		}
	}
	shutdown(sock, SD_BOTH);
	closesocket(sock);
	SetGDIState(GDIState::MainMenu);
	return 0;
}

void ShowFiles(char* files, int count);

void messageHandlerReciever(char* message, int sizeMessage, SOCKET sock) {
	int32_t count;
	static sockaddr senderAddr;
	static int32_t senderAddrLen;
	static char packBuffer[MAX_PACK_COUNT*(MAX_MESSAGE_SIZE+sizeof(int))];
	int32_t nextPack; int recievedPack;
	char type = *message,flag;
	message++;
	sizeMessage--;
	switch (type) {
	case MESSAGE_SHOWFILES:
		count = ((int32_t*)message)[0];//кол-во файлов
		message += sizeof(int32_t);
		ShowFiles(message, count);
		break;

	case MESSAGE_GETFILETCP://поток данных файла
		flag = message[0]; message++;
		sizeMessage--;
		outputFile.write(message, sizeMessage);
		if (flag & lastPack) {
			outputFile.close();
		}
		break;

	case MESSAGE_SETUDP://просьба о создании udp-сокета 
		senderAddrLen = *((int32_t*)message);
		message += sizeof(int32_t);
		copy(message, message + senderAddrLen, (char*)&senderAddr);
		//открыть брандмауэр на последующий прием от этого адреса
		SendMessageToNodeUDP(recieverUDP, &senderAddr, senderAddrLen, MESSAGE_OPENUDPFIREWALL, nullptr, 0);
		SendMessageToNode(sock, MESSAGE_UDPTRANSMISSION, nullptr, 0);
		nextPack = 0;//следующий по очереди пакет
		recievedPack = 0;//приянтых пакетов с последнего уведомления
		auto startTime = chrono::high_resolution_clock::now();
		chrono::microseconds maxMS(maxWait);
		while ((message = GetFullMessageFromNodeUDP(recieverUDP, senderAddr, senderAddrLen))) {
			char type = *message; message++; messageSize--;
			if (type == MESSAGE_UDPTRANSMISSION) {
				auto deltaTime = chrono::high_resolution_clock::now() - startTime;//прошло времени с прошлого уведомления
				flag = message[0]; message++;
				messageSize--;
				int32_t packNum = *(int32_t*)message;
				message += sizeof(int32_t);
				messageSize -= sizeof(int32_t);
				if (nextPack == packNum) {//если пакет пришел в порядке очереди
					nextPack++;
					outputFile.write(message, messageSize);
					recievedPack++;
					if (recievedPack == MAX_PACK_COUNT) {//быстро пришли все пакеты окна
						SendMessageToNodeUDP(recieverUDP, &senderAddr, senderAddrLen, MESSAGE_OPENUDPFIREWALL, (char*)&nextPack, sizeof(int32_t));
						recievedPack = 0;
					}
					else if ((chrono::duration_cast<chrono::microseconds>(deltaTime)) > maxMS) {
						SendMessageToNodeUDP(recieverUDP, &senderAddr, senderAddrLen, MESSAGE_OPENUDPFIREWALL, (char*)&nextPack, sizeof(int32_t));
						recievedPack = 0;
						startTime = chrono::high_resolution_clock::now();
					}
				}
				else {
					if ((chrono::duration_cast<chrono::microseconds>(deltaTime)) > maxMS) {
						SendMessageToNodeUDP(recieverUDP, &senderAddr, senderAddrLen, MESSAGE_OPENUDPFIREWALL, (char*)&nextPack, sizeof(int32_t));
						recievedPack = 0;
						startTime = chrono::high_resolution_clock::now();
					}
				}
				if (flag & lastPack) {
					SendMessageToNodeUDP(recieverUDP, &senderAddr, senderAddrLen, MESSAGE_OPENUDPFIREWALL, (char*)&nextPack, sizeof(int32_t));
					outputFile.close();
					closesocket(recieverUDP);
					break;
				}
			}
		}
		break;
	}
}



////////ГРАФИЧЕСКАЯ ЧАСТЬ ПРИЛОЖЕНИЯ

void InitClientWindows();//создание окон для приложения



DWORD WINAPI GDIThread(LPVOID data) {
	InitWindowFramework();
	InitClientWindows();
	isGDILoaded = 1;
	MSG msg;
	while (isActive && GetMessage(&msg,NULL,0,0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	isActive = 0;
	FinallizeFramework();
	FreeFontResourses();
	return 0;
}

const COLORREF mainColor = RGB(183, 196, 201);
const COLORREF panelColor = RGB(83, 196, 201);
const COLORREF scrollColor = RGB(83, 96, 201);

const COLORREF btnColorStatic = RGB(229, 227, 145);
const COLORREF btnColorMove = RGB(242, 239, 157);
const COLORREF btnColorPress = RGB(204, 202, 141);

const COLORREF lblColorMove = RGB(242, 239, 157);
const COLORREF lblColorPress = RGB(204, 202, 141);
const COLORREF lblColorChoose = RGB(24, 202, 141);

EditClass* editName;
ListBoxClass* listNames;
ListBoxClass* listFiles;
LabelClass* searchCaption;

ScrollBarClass* sbFiles;



void CustomFont1(Frame* frame) {
	frame->font->SetFontSize(16);
	frame->font->SetFontFamily(L"Comfortaa");
}

void CustomFont2(Frame* frame) {
	frame->font->SetFontSize(15);
	frame->font->SetFontFamily(L"Comfortaa");
}

void CustomList(ListBoxClass* list) {
	list->SetBackgroundColor(panelColor);
	list->SetBorderRadius(30);
	CustomFont1(list);
	list->complexPainter->GetPainter(painterType::Static)->SetBackgroundColor(panelColor);
	list->complexPainter->GetPainter(painterType::Move)->SetBackgroundColor(lblColorMove);
	list->complexPainter->GetPainter(painterType::Active)->SetBackgroundColor(lblColorPress);
	list->complexPainter->GetPainter(painterType::Pushed)->SetBackgroundColor(lblColorChoose);
}

void MouseMove1(Frame* btn) {
	btn->SetBackgroundColor(btnColorMove);
}

void MouseDown1(Frame* btn) {
	btn->SetBackgroundColor(btnColorPress);
}


void MouseUp1(Frame* btn) {
	btn->SetBackgroundColor(btnColorStatic);
}

void MouseLeave1(Frame* btn) {
	btn->SetBackgroundColor(btnColorStatic);
}


void btnGetName(Frame* btn) {//СОБЫТИЯ ВВОДА ИМЕНИ
	MouseUp1(btn);
	editName->GetText(clientName);
	if (!clientName.size()) {
		MessageBox(btn->hWnd, L"Вы не ввели имя", L"Ooops", MB_ICONWARNING | MB_OK);
	}
	else {
		SetEvent(getNameEvent);//разблокируем вызывавший поток
	}
}

void btnConnectEvent(Frame* btn) {//СОБЫТИЕ ПОДКЛЮЧЕНИЯ К КЛИЕНТУ
	MouseUp1(btn);
	int ID = listNames->GetItemID();
	if (ID!=-1) {
		wstring name = clientNames[ID];
		//отправить сообщение серверу, чтобы узнать адрес принимающего сокета со стороны дургого клиента
		SendMessageToNode(sock, MESSAGE_TOCONNECT, (char*)&name[0], (name.size() + 1) * sizeof(wchar_t));
	}
	else {
		MessageBox(mainWindow->hWnd, L"Вы не выбрали клиента", L"Ooops", MB_ICONWARNING | MB_OK);
	}
}

void btnGetDirInfo(Frame* btn) {//ПОЛУЧЕНИЕ ФАЙЛОВ КАТАЛОГА(НАЖАТИЕ НА ЛИСТ)
	int id = listFiles->GetItemID();
		if (id>=0 && id<filesReciever.size() && filesReciever[id].type == FILE_ATTRIBUTE_DIRECTORY) {
			//запрос на новые данные
			wstring dir = filesReciever[id].fileName;
			char buf[MAX_PATH + 2];
			int size = (dir.size() + 1) * sizeof(wchar_t);
			copy((char*)&dir[0], (char*)&dir[0] + size, buf);
			listFiles->SetItemID(-1);
			SendMessageToNode(senderSocket, MESSAGE_GETNEXTFILE, buf, size);
		}
}

void btnGetFileTCP(Frame* btn) {//ЗАПРОС НА ПОЛУЧЕНИЕ ФАЙЛА 
	int id = listFiles->GetItemID();
	if (id != -1) {
		if (filesReciever[id].type != FILE_ATTRIBUTE_DIRECTORY) {
			//запрос на новые данные
			wstring fileName = filesReciever[id].fileName;
			outputFile.open(fileName, ios::binary | ios::out | ios::trunc);
			char buf[MAX_PATH + 2];
			int size = (fileName.size() + 1) * sizeof(wchar_t);
			copy((char*)&fileName[0], (char*)&fileName[0] + size, buf);
			SendMessageToNode(senderSocket, MESSAGE_GETFILETCP, buf, size);
		}
	}
}

void btnGetFileUDP(Frame* btn) {//ЗАПРОС НА ПОЛУЧЕНИЕ ФАЙЛА 
	int id = listFiles->GetItemID();
	if (id != -1) {
		if (filesReciever[id].type != FILE_ATTRIBUTE_DIRECTORY) {
			//запрос на новые данные
			wstring fileName = filesReciever[id].fileName;
			outputFile.open(fileName, ios::binary | ios::out | ios::trunc);
			char buf[MAX_PATH*2+sizeof(sockaddr)+sizeof(int32_t)];
			int size = (fileName.size() + 1) * sizeof(wchar_t);
			copy((char*)&fileName[0], (char*)&fileName[0] + size, buf);
			//получение адреса получателя
			recieverUDP = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (recieverUDP == INVALID_SOCKET) {
				outputFile.close();
				return;
			}
			lenRecAddr = sizeof(sockaddr);
			if (getsockname(senderSocket, &recieverAddr, &lenRecAddr) == 0) {
				if (bind(recieverUDP, &recieverAddr, lenRecAddr) != 0) {
					closesocket(recieverUDP);
					outputFile.close();
					return;
				}
				int32_t len32 = lenRecAddr;
				((int32_t*)(&buf[size]))[0] = len32;
				copy((char*)&recieverAddr, (char*)&recieverAddr + lenRecAddr, &buf[size+sizeof(len32)]);
				SendMessageToNode(senderSocket, MESSAGE_GETFILEUDP, buf, size + sizeof(len32)+len32);
			}
			else {
				int err = WSAGetLastError();
				MessageBox(NULL, L"Невозвожно получить адрес клиента",L"Ooops",MB_OK);
			}
		}
	}
}

void btnShutdownConnection(Frame* btn) {//разорвать соединение
	shutdown(senderSocket, SD_BOTH);
}

void CustomBtn1(Frame* btn) {
	CustomFont1(btn);
	btn->SetBackgroundColor(btnColorStatic);
	btn->SetBorderRadius(30);
	btn->eventHandler->SetMouseEvent(MouseEvents::OnMove, MouseMove1);
	btn->eventHandler->SetMouseEvent(MouseEvents::OnLeave, MouseLeave1);
	btn->eventHandler->SetMouseEvent(MouseEvents::OnLBDown, MouseDown1);
	btn->eventHandler->SetMouseEvent(MouseEvents::OnClick, MouseUp1);
}


void ResetNames() {
	static int lastNameCount = 0;
	if (gdiState == GDIState::MainMenu) {
		for (int i = lastNameCount; i >0; i--) {
			listNames->RemoveItem(i - 1);
		}
		for (int i = 0; i < clientNames.size(); i++) {
			listNames->AddItem(clientNames[i], 1);
		}
		lastNameCount = clientNames.size();
		listNames->Repaint();
	}
}

int fileCount = 0;//кол-во файлов для скрола
const int maxFileLine = 6;
const int scrollRange = 100;

void ScrollFiles(Frame* scroll, int thumbPos) {
	int df = fileCount - maxFileLine;
	if (df > 0) {
		int dy = scrollRange / (df);
		float scroll = (float)thumbPos / dy;
		listFiles->SetListScroll(scroll);
	}
}

void InitClientWindows() {
	AddFontFamily(L"../Comfortaa-Regular.ttf");
	//ГЛАВНОЕ ОКНО
	mainWindow= new WindowClass(L"MainWindow", Window::WindowType::MainWindow, Position::absoluteSize, 10, 10, 800, 500, NULL);
	mainWindow->SetBackgroundColor(mainColor);
	//название
	LabelClass* mainCaption = new LabelClass(L"Проводник", Position::absolutePosH, 0, 10, 100, 60, mainWindow);
	CustomFont1(mainCaption);
	mainCaption->SetBackgroundColor(mainColor);
	mainCaption->SetTextAlign(TextAlign::Center);
	mainWindow->AddChild(mainCaption);
	//текст
	LabelClass* label1 = new LabelClass(L"Доступные пользователи: ", Position::absolutePosH, 30, 70, 100, 60, mainWindow);
	CustomFont2(label1);
	label1->SetBackgroundColor(mainColor);
	mainWindow->AddChild(label1);
	
	//надиси имен пользователей
	listNames = new ListBoxClass(Position::absoluteAll, 30, 130, 700, 250, mainWindow);
	
	listNames->SetListInfo(4, 0);
	CustomList(listNames);
	mainWindow->AddChild(listNames);
	
	//кнопка подключения
	Button* connectBtn = new Button(Position::absoluteAll, 275, 400, 250, 50, mainWindow);
	connectBtn->SetCaption(L"Подключиться");
	CustomBtn1(connectBtn);
	connectBtn->eventHandler->SetMouseEvent(MouseEvents::OnClick, btnConnectEvent);
	mainWindow->AddChild(connectBtn);

	//ОКНО ПРОСМОТРА ФАЙЛОВ
	searchWindowMain = new WindowClass(L"Проводник", Window::WindowType::PanelWindow, Position::absoluteSize, 10, 10, 800, 500, NULL);
	searchWindowMain->SetBackgroundColor(mainColor);
	WindowClass* searchWindow = new WindowClass(L"Проводник", Window::WindowType::PanelWindow, Position::absoluteAll, 0, 40, 800, 500,searchWindowMain);
	searchWindow->SetBackgroundColor(mainColor);
	searchWindowMain->AddChild(searchWindow);
	//CAption
	CaptionClass* caption = new CaptionClass(40, searchWindowMain);
	searchWindowMain->AddChild(caption);
	caption->SetBackgroundColor(RGB(100, 200, 100));
	caption->font->SetFontFamily(L"Comfortaa");
	//название
	searchCaption = new LabelClass(L"Связь с клиентом", Position::absolutePosH, 0, 10, 100, 60, searchWindow);
	CustomFont1(searchCaption);
	searchCaption->SetBackgroundColor(mainColor);
	searchCaption->SetTextAlign(TextAlign::Center);
	searchCaption->SetSingleLine(false);
	searchWindow->AddChild(searchCaption);
	//лист файлов
	listFiles = new ListBoxClass(Position::absoluteAll, 50, 70, 640, 300, searchWindow);
	CustomList(listFiles);
	listFiles->SetListInfo(maxFileLine, 0);
	listFiles->eventHandler->SetMouseEvent(MouseEvents::OnClick, btnGetDirInfo);
	searchWindow->AddChild(listFiles);
	//scroll
	sbFiles = new ScrollBarClass(Position::absoluteAll, 690, 70, 60, 300, searchWindow);
	sbFiles->SetScrollInfo(0, scrollRange, 0, 30);
	sbFiles->SetBackgroundColor(scrollColor);
	sbFiles->SetScrollEvent(ScrollFiles);
	searchWindow->AddChild(sbFiles);
	//
	Button* btnTCP = new Button(Position::absoluteAll, 50, 390, 200, 50, searchWindow);
	CustomBtn1(btnTCP);
	CustomFont2(btnTCP);
	btnTCP->SetCaption(L"Скачать (TCP)");
	btnTCP->eventHandler->SetMouseEvent(MouseEvents::OnClick, btnGetFileTCP);
	searchWindow->AddChild(btnTCP);
	//
	Button* btnUDP = new Button(Position::absoluteAll, 300, 390, 200, 50, searchWindow);
	CustomBtn1(btnUDP);
	CustomFont2(btnUDP);
	btnUDP->SetCaption(L"Скачать (UDP)");
	btnUDP->eventHandler->SetMouseEvent(MouseEvents::OnClick, btnGetFileUDP);
	searchWindow->AddChild(btnUDP);
	//
	Button* btnDisconnect = new Button(Position::absoluteAll, 550, 390, 200, 50, searchWindow);
	CustomBtn1(btnDisconnect);
	btnDisconnect->font->SetFontSize(11);
	btnDisconnect->SetCaption(L"Разорвать соединение");
	btnDisconnect->eventHandler->SetMouseEvent(MouseEvents::OnClick, btnShutdownConnection);
	searchWindow->AddChild(btnDisconnect);



	//ДИАЛОГ ЗАПРОСА ИМЕНИ
	getNameDialog = new WindowClass(L"Name", Window::WindowType::PanelWindow, Position::absoluteSize, 10, 10, 450, 250, NULL);
	getNameDialog->SetBackgroundColor(mainColor);
	//текст
	LabelClass* lblCaption = new LabelClass(L"Введите имя устройства:", Position::absoluteAll, 20, 20, 400, 50, getNameDialog);
	CustomFont1(lblCaption);
	lblCaption->SetBackgroundColor(mainColor);
	getNameDialog->AddChild(lblCaption);
	//поле ввода
	editName = new EditClass(L"", Position::absoluteAll, 20, 70, 400, 50,getNameDialog);
	CustomFont1(editName);
	editName->SetBackgroundColor(RGB(160, 184, 250));
	editName->SetBorderRadius(10);
	getNameDialog->AddChild(editName);
	//кнопка подтверждения
	Button* submitBtn = new Button(Position::absoluteAll, 100, 140, 250, 50, getNameDialog);
	CustomFont1(submitBtn);
	submitBtn->SetCaption(L"Подтвердить");
	CustomBtn1(submitBtn);
	submitBtn->eventHandler->SetMouseEvent(MouseEvents::OnClick, btnGetName);
	getNameDialog->AddChild(submitBtn);

	/// <summary>
	/// /////////////
	/// </summary>
	/// //получить рутовую папку
	CoInitialize(NULL);
	char folderName[MAX_PATH];
	BROWSEINFOA bi;
	bi.hwndOwner = NULL;
	bi.pidlRoot = NULL;
	bi.pszDisplayName = folderName;
	bi.lpszTitle = NULL;
	bi.ulFlags = 0;
	bi.lpfn = NULL;
	bi.iImage = 0;
	PIDLIST_ABSOLUTE idList = NULL;
	while (idList == NULL) {
		idList= SHBrowseForFolderA(&bi);
	}
	WCHAR rootFile[MAX_PATH];
	SHGetPathFromIDList(idList, rootFile);
	rootDirectory = rootFile;
	CoUninitialize();

	mainWindow->CreateWnd();
	searchWindowMain->CreateWnd();
	getNameDialog->CreateWnd();

	//gdiState = GDIState::GetNameDialog;
	//mainWindow->HideWindow();
}

void SetGDIState(GDIState state) {
	switch (state) {
	case GDIState::GetNameDialog:
		getNameDialog->ShowWindow();
		mainWindow->HideWindow();
		searchWindowMain->HideWindow();
		break;
	case GDIState::MainMenu:
		getNameDialog->HideWindow();
		mainWindow->ShowWindow();
		searchWindowMain->HideWindow();
		break;
	case GDIState::Search:
		getNameDialog->HideWindow();
		mainWindow->HideWindow();
		searchWindowMain->ShowWindow();
		searchCaption->SetCaption(L"Связь с клиентом " + clientNames[listNames->GetItemID()]);
		break;
	}
}

void ShowFiles(char* files, int count) {//вывести файлы в список
	listFiles->ClearList();
	filesReciever.clear();
	fileCount = count;
	
	
	wstring name; int32_t type;
	int off = 0;
	
	for (int i = 0; i < count; i++) {
		name = (wchar_t*)(&files[off]);
		off += (name.size() + 1) * sizeof(wchar_t);
		type = ((int32_t*)(&(files[off])))[0];
		off += sizeof(int32_t);
		filesReciever.push_back({ name,type });
		if (type == FILE_ATTRIBUTE_DIRECTORY) {
			name += L" (директория)";
		}
		listFiles->AddItem(name, 1);
		
	}
	listFiles->SetItemID(-1);
	listFiles->SetListScroll(0);
	sbFiles->SetScrollInfo(0, scrollRange, 0, 30);
	listFiles->Repaint();
}
