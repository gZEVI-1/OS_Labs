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

vector<string> splitIntoWords(const string& text) {
    vector<string> result;
    stringstream ss(text);
    string word;
    while (ss >> word) {
        result.push_back(word);
    }
    return result;
}

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


bool startClientProcess(int clientId) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    string cmdLine = "Client.exe " + to_string(clientId);

    BOOL result = CreateProcessA(
        NULL,                           // имя приложения
        (LPSTR)cmdLine.c_str(), 
        NULL,                           // атрибуты процесса
        NULL,                           // атрибуты потока
        FALSE,                          // наследование дескрипторов
        CREATE_NEW_CONSOLE,
        NULL,                           // окружение
        NULL,                           // каталог
        &si,                            // STARTUPINFO
        &pi                             // PROCESS_INFORMATION
    );

    if (!result) {
        cerr << "Failed to start client " << clientId << ". Error: " << GetLastError() << endl;
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    cout << "Client " << clientId << " process started." << endl;
    return true;
}

bool sendWordToClient(int clientIndex, const string& word) {
    if (clientIndex >= clients.size() || !clients[clientIndex].connected) {
        return false;
    }

    int wordLen = word.length();

    int sendResult = send(clients[clientIndex].socket, (char*)&wordLen, sizeof(int), 0);
    if (sendResult == SOCKET_ERROR) {
        clients[clientIndex].connected = false;
        return false;
    }


    sendResult = send(clients[clientIndex].socket, word.c_str(), wordLen, 0);
    if (sendResult == SOCKET_ERROR) {
        clients[clientIndex].connected = false;
        return false;
    }

    cout << "Sent word [" << word << "] to client " << clients[clientIndex].id << endl;
    return true;
}

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

    while (currentWordIndex < words.size()) {
        int clientIndex = currentWordIndex % numClients;

        if (clients[clientIndex].connected) {
            if (!sendWordToClient(clientIndex, words[currentWordIndex])) {
                cout << "Client " << clients[clientIndex].id << " disconnected." << endl;
            }
        }

        currentWordIndex++;
        Sleep(100); 
    }

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

    WSAData wsaData;
    WORD DLLVersion = MAKEWORD(2, 2);
    if (WSAStartup(DLLVersion, &wsaData) != 0) {
        cerr << "WSAStartup failed!" << endl;
        return 1;
    }


    string filename;
    cout << "Enter filename: ";
    getline(cin, filename);


    if (!readFile(filename)) {
        WSACleanup();
        return 1;
    }


    int numClients;
    cout << "Enter number of clients (minimum 3): ";
    cin >> numClients;
    if (numClients < 3) {
        cout << "Minimum 3 clients required. Setting to 3." << endl;
        //numClients = 3;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET) {
        cerr << "Socket creation failed!" << endl;
        WSACleanup();
        return 1;
    }

    SOCKADDR_IN addr;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(1111);
    addr.sin_family = AF_INET;


    if (bind(listenSocket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        cerr << "Bind failed! Error: " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }


    if (listen(listenSocket, numClients) == SOCKET_ERROR) {
        cerr << "Listen failed! Error: " << WSAGetLastError() << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    cout << "Server started. Waiting for " << numClients << " clients..." << endl;

    for (int i = 1; i <= numClients; i++) {
        if (!startClientProcess(i)) {
            cerr << "Failed to start client " << i << endl;
        }
        Sleep(500); 
    }

    clients.resize(numClients);
    int connectedClients = 0;

    while (connectedClients < numClients) {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "Accept failed! Error: " << WSAGetLastError() << endl;
            continue;
        }

        int clientId;
        int recvResult = recv(clientSocket, (char*)&clientId, sizeof(int), 0);
        if (recvResult <= 0) {
            closesocket(clientSocket);
            continue;
        }

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

    distributeWords();

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