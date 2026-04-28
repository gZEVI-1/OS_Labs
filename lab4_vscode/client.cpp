#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#pragma warning(disable: 4996)

using namespace std;



int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    int clientId = 1;
    if (argc > 1) {
        clientId = atoi(argv[1]);
    }


    WSAData wsaData;
    WORD DLLVersion = MAKEWORD(2, 2);
    if (WSAStartup(DLLVersion, &wsaData) != 0) {
        cerr << "WSAStartup failed!" << endl;
        return 1;
    }


    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed!" << endl;
        WSACleanup();
        return 1;
    }

    SOCKADDR_IN serverAddr;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddr.sin_port = htons(1111);
    serverAddr.sin_family = AF_INET;

    cout << "Client " << clientId << ": Connecting to server..." << endl;

    int attempts = 0;
    while (connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        if (attempts++ > 20) {
            cerr << "Client " << clientId << ": Failed to connect to server!" << endl;
            closesocket(clientSocket);
            WSACleanup();
            return 1;
        }
        Sleep(500); 
    }

    cout << "Client " << clientId << ": Connected to server!" << endl;

    send(clientSocket, (char*)&clientId, sizeof(int), 0);

    cout << "Client " << clientId << ": Receiving words..." << endl;
    cout << "----------------------------------------" << endl;

    while (true) {
        int wordLen;
        int recvResult = recv(clientSocket, (char*)&wordLen, sizeof(int), 0);

        if (recvResult <= 0 || wordLen == -1) {
            break;
        }

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