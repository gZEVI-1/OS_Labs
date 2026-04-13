#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#pragma warning(disable: 4996)

using namespace std;

#define PORT 1111
#define SERVER_IP "127.0.0.1"

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // Получаем ID клиента из аргументов командной строки
    int clientId = 1;
    if (argc > 1) {
        clientId = atoi(argv[1]);
    }

    // Инициализация Winsock
    WSAData wsaData;
    WORD DLLVersion = MAKEWORD(2, 2);
    if (WSAStartup(DLLVersion, &wsaData) != 0) {
        cerr << "WSAStartup failed!" << endl;
        return 1;
    }

    // Создаем сокет
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed!" << endl;
        WSACleanup();
        return 1;
    }

    // Настройка адреса сервера
    SOCKADDR_IN serverAddr;
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_family = AF_INET;

    // Подключаемся к серверу
    cout << "Client " << clientId << ": Connecting to server..." << endl;

    int attempts = 0;
    while (connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        if (attempts++ > 20) {
            cerr << "Client " << clientId << ": Failed to connect to server!" << endl;
            closesocket(clientSocket);
            WSACleanup();
            return 1;
        }
        Sleep(500); // Ждем, пока сервер запустится
    }

    cout << "Client " << clientId << ": Connected to server!" << endl;

    // Отправляем серверу свой ID
    send(clientSocket, (char*)&clientId, sizeof(int), 0);

    // Получаем слова от сервера
    cout << "Client " << clientId << ": Receiving words..." << endl;
    cout << "----------------------------------------" << endl;

    while (true) {
        // Получаем длину слова
        int wordLen;
        int recvResult = recv(clientSocket, (char*)&wordLen, sizeof(int), 0);

        if (recvResult <= 0 || wordLen == -1) {
            // Сигнал завершения или ошибка
            break;
        }

        // Получаем само слово
        char* buffer = new char[wordLen + 1];
        recvResult = recv(clientSocket, buffer, wordLen, 0);

        if (recvResult <= 0) {
            delete[] buffer;
            break;
        }

        buffer[wordLen] = '\0';
        cout << "Client " << clientId << " received: " << buffer << endl;
        delete[] buffer;
    }

    cout << "----------------------------------------" << endl;
    cout << "Client " << clientId << ": Finished." << endl;

    closesocket(clientSocket);
    WSACleanup();

    system("pause");
    return 0;
}