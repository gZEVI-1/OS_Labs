#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#pragma warning(disable: 4996)

using namespace std;

#define PORT 1111
#define BUFFER_SIZE 1024

struct ClientInfo {
    SOCKET socket;
    int id;
    bool connected;
};

vector<ClientInfo> clients;
vector<string> words;
int currentWordIndex = 0;

// Функция для разбиения строки на слова
vector<string> splitIntoWords(const string& text) {
    vector<string> result;
    stringstream ss(text);
    string word;
    while (ss >> word) {
        result.push_back(word);
    }
    return result;
}

// Функция для чтения файла
bool readFile(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to open file: " << filename << endl;
        return false;
    }

    string line;
    string fullText;
    while (getline(file, line)) {
        fullText += line + " ";
    }
    file.close();

    words = splitIntoWords(fullText);
    cout << "File loaded. Total words: " << words.size() << endl;
    return true;
}

// Функция для запуска клиентского процесса
bool startClientProcess(int clientId) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Формируем командную строку для клиента
    string cmdLine = "Client.exe " + to_string(clientId);

    BOOL result = CreateProcessA(
        NULL,                           // Имя приложения
        (LPSTR)cmdLine.c_str(),         // Командная строка
        NULL,                           // Атрибуты процесса
        NULL,                           // Атрибуты потока
        FALSE,                          // Наследование дескрипторов
        CREATE_NEW_CONSOLE,             // Флаги создания (новая консоль)
        NULL,                           // Окружение
        NULL,                           // Текущий каталог
        &si,                            // STARTUPINFO
        &pi                             // PROCESS_INFORMATION
    );

    if (!result) {
        cerr << "Failed to start client " << clientId << ". Error: " << GetLastError() << endl;
        return false;
    }

    // Закрываем дескрипторы (процесс продолжает работать)
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    cout << "Client " << clientId << " process started." << endl;
    return true;
}

// Функция для отправки слова клиенту
bool sendWordToClient(int clientIndex, const string& word) {
    if (clientIndex >= clients.size() || !clients[clientIndex].connected) {
        return false;
    }

    int wordLen = word.length();
    // Сначала отправляем длину слова
    int sendResult = send(clients[clientIndex].socket, (char*)&wordLen, sizeof(int), 0);
    if (sendResult == SOCKET_ERROR) {
        clients[clientIndex].connected = false;
        return false;
    }

    // Затем отправляем само слово
    sendResult = send(clients[clientIndex].socket, word.c_str(), wordLen, 0);
    if (sendResult == SOCKET_ERROR) {
        clients[clientIndex].connected = false;
        return false;
    }

    cout << "Sent word [" << word << "] to client " << clients[clientIndex].id << endl;
    return true;
}

// Функция для распределения слов по клиентам
void distributeWords() {
    int numClients = clients.size();
    int activeClients = 0;

    for (int i = 0; i < numClients; i++) {
        if (clients[i].connected) activeClients++;
    }

    if (activeClients == 0) {
        cout << "No active clients!" << endl;
        return;
    }

    // Распределяем слова по круговому принципу
    while (currentWordIndex < words.size()) {
        int clientIndex = currentWordIndex % numClients;

        if (clients[clientIndex].connected) {
            if (!sendWordToClient(clientIndex, words[currentWordIndex])) {
                cout << "Client " << clients[clientIndex].id << " disconnected." << endl;
            }
        }

        currentWordIndex++;
        Sleep(100); // Небольшая задержка для стабильности
    }

    // Отправляем сигнал завершения всем клиентам (длина -1)
    int endSignal = -1;
    for (int i = 0; i < numClients; i++) {
        if (clients[i].connected) {
            send(clients[i].socket, (char*)&endSignal, sizeof(int), 0);
        }
    }

    cout << "All words distributed!" << endl;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // Инициализация Winsock
    WSAData wsaData;
    WORD DLLVersion = MAKEWORD(2, 2);
    if (WSAStartup(DLLVersion, &wsaData) != 0) {
        cerr << "WSAStartup failed!" << endl;
        return 1;
    }

    // Ввод имени файла
    string filename;
    cout << "Enter filename: ";
    getline(cin, filename);

    // Читаем файл
    if (!readFile(filename)) {
        WSACleanup();
        return 1;
    }

    // Ввод количества клиентов
    int numClients;
    cout << "Enter number of clients (minimum 3): ";
    cin >> numClients;
    if (numClients < 3) {
        cout << "Minimum 3 clients required. Setting to 3." << endl;
        //numClients = 3;
    }

    // Создаем сокет для прослушивания
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed!" << endl;
        WSACleanup();
        return 1;
    }

    // Настройка адреса
    SOCKADDR_IN addr;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(PORT);
    addr.sin_family = AF_INET;

    // Привязка сокета
    if (bind(listenSocket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cerr << "Bind failed! Error: " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // Начинаем прослушивание
    if (listen(listenSocket, numClients) == SOCKET_ERROR) {
        cerr << "Listen failed! Error: " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    cout << "Server started. Waiting for " << numClients << " clients..." << endl;

    // Запускаем клиентские процессы
    for (int i = 1; i <= numClients; i++) {
        if (!startClientProcess(i)) {
            cerr << "Failed to start client " << i << endl;
        }
        Sleep(500); // Даем время на запуск
    }

    // Принимаем подключения от клиентов
    clients.resize(numClients);
    int connectedClients = 0;

    while (connectedClients < numClients) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "Accept failed! Error: " << WSAGetLastError() << endl;
            continue;
        }

        // Получаем ID клиента
        int clientId;
        int recvResult = recv(clientSocket, (char*)&clientId, sizeof(int), 0);
        if (recvResult <= 0) {
            closesocket(clientSocket);
            continue;
        }

        // Сохраняем информацию о клиенте
        if (clientId >= 1 && clientId <= numClients) {
            clients[clientId - 1].socket = clientSocket;
            clients[clientId - 1].id = clientId;
            clients[clientId - 1].connected = true;
            connectedClients++;
            cout << "Client " << clientId << " connected. (" << connectedClients
                << "/" << numClients << ")" << endl;
        }
        else {
            closesocket(clientSocket);
        }
    }

    cout << "All clients connected. Starting distribution..." << endl;

    // Распределяем слова
    distributeWords();

    // Закрываем соединения
    for (int i = 0; i < numClients; i++) {
        if (clients[i].connected) {
            closesocket(clients[i].socket);
        }
    }

    closesocket(listenSocket);
    WSACleanup();

    cout << "Server finished." << endl;
    system("pause");
    return 0;
}