#include <iostream>
#include <string>
#include <winsock2.h>
#include <windows.h>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable: 4996)

using namespace std;

// Глобальные объекты синхронизации
HANDLE hMutex;             // Для доступа к критической секции (работа с 1 клиентом)
HANDLE hClientDisconnected; // Событие: клиент завершил сеанс

// Прототипы функций
string decimalToBinary(int n);
string decimalToHex(int n);
string translateNumber(const string& request, string& clientId);
DWORD WINAPI ClientHandler(LPVOID lpParam);

int main()
{
    // Загрузка Winsock
    WSADATA wsaData;
    WORD DLLVersion = MAKEWORD(2, 1);
    if (WSAStartup(DLLVersion, &wsaData) != 0) {
        cerr << "Winsock library loading failed." << endl;
        return 1;
    }

    // Ввод количества клиентов
    int totalClients;
    cout << "Enter total number of clients: ";
    cin >> totalClients;

    // Создание мьютекса (изначально свободен)
    hMutex = CreateMutex(NULL, FALSE, NULL);
    if (hMutex == NULL) {
        cerr << "CreateMutex failed: " << GetLastError() << endl;
        return 1;
    }

    // Создание события с автосбросом для сигнала о завершении сеанса клиента
    hClientDisconnected = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (hClientDisconnected == NULL) {
        cerr << "CreateEvent failed: " << GetLastError() << endl;
        return 1;
    }

    // Настройка сокета
    SOCKADDR_IN addr;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(1111);
    addr.sin_family = AF_INET;

    SOCKET sListen = socket(AF_INET, SOCK_STREAM, NULL);
    if (bind(sListen, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closesocket(sListen);
        WSACleanup();
        return 1;
    }

    if (listen(sListen, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Listen failed." << endl;
        closesocket(sListen);
        WSACleanup();
        return 1;
    }

    cout << "Server started. Waiting for " << totalClients << " clients..." << endl;

    // Основной цикл приема клиентов
    for (int i = 0; i < totalClients+1; i++)
    {
        SOCKET newConnection;
        if ((newConnection = accept(sListen, NULL, NULL)) == INVALID_SOCKET) {
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            continue;
        }

        cout << "Client connected. Waiting for mutex..." << endl;
        
        // Создаем поток для обработки клиента
        DWORD threadId;
        HANDLE hThread = CreateThread(NULL, 0, ClientHandler, (LPVOID)&newConnection, 0, &threadId);
        if (hThread == NULL) {
            cerr << "CreateThread failed." << endl;
            closesocket(newConnection);
        }
        else {
            CloseHandle(hThread); // Отсоединяемся от потока, он будет выполняться сам
        }
    }

    cout << "All clients processed. Server shutting down." << endl;
    
    // Очистка
    closesocket(sListen);
    CloseHandle(hMutex);
    CloseHandle(hClientDisconnected);
    WSACleanup();
    system("pause");
    return 0;
}

// Функция обработки клиента в отдельном потоке
DWORD WINAPI ClientHandler(LPVOID lpParam)
{
    SOCKET clientSocket = *(SOCKET*)lpParam;
    char buffer[256];
    string clientId = "Unknown";
    
    // Получаем идентификатор клиента (первое сообщение)
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        clientId = string(buffer);
        cout << "Client ID: " << clientId << endl;
    }

    bool sessionActive = true;
    while (sessionActive)
    {
        // Ждем освобождения мьютекса для работы с этим клиентом
        WaitForSingleObject(hMutex, INFINITE);
        cout << "Mutex acquired by: " << clientId << endl;

        // Отправляем клиенту разрешение на передачу данных
        send(clientSocket, "READY", 5, 0);

        // Получаем запрос от клиента (например, "to_bin 42" или "to_hex 255")
        bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0) {
            cerr << "Client disconnected." << endl;
            ReleaseMutex(hMutex);
            break;
        }
        buffer[bytesReceived] = '\0';
        string request(buffer);
        
        // Проверка на завершение сеанса
        if (request == "END_SESSION") {
            cout << clientId << " requested end of session." << endl;
            sessionActive = false;
            send(clientSocket, "SESSION_ENDED", 13, 0);
        }
        else {
            // Обработка запроса
            string result = translateNumber(request, clientId);
            // Отправляем результат клиенту
            send(clientSocket, result.c_str(), result.length(), 0);
            cout << "Result sent to " << clientId << ": " << result << endl;
        }

        // Освобождаем мьютекс, давая возможность подключиться другим клиентам
        ReleaseMutex(hMutex);
        cout << "Mutex released by: " << clientId << endl;
        
        // Сигнализируем, что клиент завершил текущую операцию
        SetEvent(hClientDisconnected);
        
        // Небольшая пауза, чтобы другой клиент мог перехватить мьютекс
        Sleep(100);
    }

    closesocket(clientSocket);
    return 0;
}

// Вспомогательные функции перевода
string decimalToBinary(int n) {
    if (n == 0) return "0";
    string binary = "";
    while (n > 0) {
        binary = (n % 2 == 0 ? "0" : "1") + binary;
        n /= 2;
    }
    return binary;
}

string decimalToHex(int n) {
    stringstream ss;
    ss << uppercase << hex << n;
    return ss.str();
}

string translateNumber(const string& request, string& clientId) {
    stringstream ss(request);
    string command;
    int number;
    ss >> command >> number;
    
    string result = "From client " + clientId + ": ";
    if (command == "to_bin") {
        result += "DEC " + to_string(number) + " -> BIN " + decimalToBinary(number);
    }
    else if (command == "to_hex") {
        result += "DEC " + to_string(number) + " -> HEX " + decimalToHex(number);
    }
    else {
        result += "Unknown command";
    }
    return result;
}