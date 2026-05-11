#include <iostream>
#include <string>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable: 4996)

using namespace std;

int main(int argc, char* argv[])
{
    // Проверяем, передан ли ID клиента через командную строку
    string clientId;
    if (argc > 1) {
        clientId = argv[1];
    }
    else {
        cout << "Enter client ID (e.g., Client_1): ";
        getline(cin, clientId);
    }

    // Загрузка Winsock
    WSADATA wsaData;
    WORD DLLVersion = MAKEWORD(2, 1);
    if (WSAStartup(DLLVersion, &wsaData) != 0) {
        cerr << "Winsock loading failed." << endl;
        return 1;
    }

    SOCKADDR_IN addr;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(1111);
    addr.sin_family = AF_INET;

    SOCKET s = socket(AF_INET, SOCK_STREAM, NULL);
    if (connect(s, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cerr << "Connection failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    cout << "Connected to server as " << clientId << endl;

    // Отправляем серверу свой идентификатор
    send(s, clientId.c_str(), clientId.length(), 0);

    char buffer[256];
    string command;
    
    while (true)
    {
        // Ждем сигнала READY от сервера
        int bytesReceived = recv(s, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            cerr << "Server disconnected." << endl;
            break;
        }
        buffer[bytesReceived] = '\0';
        string signal(buffer);
        
        if (signal == "SESSION_ENDED") {
            cout << "Session ended. You can reconnect or close client." << endl;
            // Ждем перед возможным переподключением
            Sleep(1000);
            continue;
        }

        if (signal != "READY") {
            cerr << "Unexpected signal: " << signal << endl;
            continue;
        }

        // Отображаем меню
        cout << "\nEnter command: 'to_bin <number>' or 'to_hex <number>' or 'end'" << endl;
        cout << "> ";
        getline(cin, command);

        if (command == "end" || command == "END_SESSION") {
            send(s, "END_SESSION", 11, 0);
            // Получаем подтверждение
            bytesReceived = recv(s, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                cout << "Server: " << buffer << endl;
            }
            cout << "You can now reconnect if needed." << endl;
            Sleep(1000);
            continue;
        }

        // Отправляем команду на сервер
        send(s, command.c_str(), command.length(), 0);

        // Получаем результат
        bytesReceived = recv(s, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            cout << "Server response: " << buffer << endl;
        }
    }

    closesocket(s);
    WSACleanup();
    return 0;
}