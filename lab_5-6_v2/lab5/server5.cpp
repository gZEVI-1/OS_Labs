#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable: 4996)

using namespace std;


string convertNumber(const string& input) {
    istringstream iss(input);
    long long num;
    string type;
    iss >> num >> type;
    
    if (iss.fail()) 
        return "Error: invalid format. Use: <number> <bin|hex>";
    
    string res;
    if (type == "bin" || type == "BIN") {
        if (num < 0) return "Error: negative number for bin";
        if (num == 0) return "0b0";
        while (num > 0) {
            res = char('0' + (num & 1)) + res;
            num >>= 1;
        }
        return "0b" + res;
    } 
    else if (type == "hex" || type == "HEX") {
        if (num == 0) return "0x0";
        bool neg = (num < 0);
        if (neg) num = -num;
        while (num > 0) {
            int d = num % 16;
            char c = (d < 10) ? ('0' + d) : ('A' + d - 10);
            res = char(c) + res;
            num /= 16;
        }
        return (neg ? "-" : "") + string("0x") + res;
    }
    return "Error: type must be 'bin' or 'hex'";
}

bool printCausedBy(int Result, const char* nameOfOper) {
    if (!Result) {
        cout << "Connection closed by client.\n";
        return false;
    } else if (Result < 0) {
        cout << nameOfOper << " failed: " << WSAGetLastError() << endl;
        return false;
    }
    return true;
}

int main() {
    WSADATA wsaData;
    WORD DLLVersion = MAKEWORD(2, 1);
    if (WSAStartup(DLLVersion, &wsaData) != 0) {
        cerr << "Error: failed to link library.\n";
        return 1;
    }

    SOCKADDR_IN addr;
    static int sizeOfAddr = sizeof(addr);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(1111);
    addr.sin_family = AF_INET;

    SOCKET sListen = socket(AF_INET, SOCK_STREAM, NULL);
    if (bind(sListen, (SOCKADDR*)&addr, sizeOfAddr) == SOCKET_ERROR) {
        cerr << "Bind error: " << WSAGetLastError() << endl;
        closesocket(sListen);
        WSACleanup();
        return 1;
    }

    if (listen(sListen, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Listen failed.\n";
        closesocket(sListen);
        WSACleanup();
        return 1;
    }

    HANDLE hSemaphore = CreateSemaphore(NULL, 1, 1, "Lab");
    if (hSemaphore == NULL) {
        cerr << "CreateSemaphore error: " << GetLastError() << endl;
        return 1;
    }

    HANDLE pool[3] = {
        CreateEvent(NULL, FALSE, FALSE, "Start"),
        CreateEvent(NULL, FALSE, FALSE, "Data"), 
        CreateEvent(NULL, FALSE, FALSE, "End")
    };
    for (int i = 0; i < 3; ++i) {
        if (!pool[i]) {
            cerr << "CreateEvent error: " << GetLastError() << endl;
            return 1;
        }
    }

    int n;
    cout << "Enter the number of clients (1 to 10) >> ";
    cin >> n;
    while (cin.fail() || n < 1 || n > 10 || cin.peek() != '\n') {
        cin.clear();
        cin.ignore(32768, '\n');
        cout << "Invalid input. Try again >> ";
        cin >> n;
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    for (int i = 0; i < n; i++) {
        wchar_t cmdLine[256];
        wcscpy_s(cmdLine, L"Client.exe");
        if (!CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE,
            CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
            cerr << "CreateProcess failed: " << GetLastError() << endl;
            return 1;
        }
        Sleep(100);
    }

    vector<SOCKET> Sockets(n);
    size_t client_number = 0;
    size_t c_num = 0;

    for (int i = 0; i < n; i++) {
        Sockets[i] = accept(sListen, (SOCKADDR*)&addr, &sizeOfAddr);
        if (Sockets[i] == INVALID_SOCKET) {
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            return 1;
        }
        cout << "Client " << i << " connected.\n";
    }

    int active = n;
    while (active > 0) {
        int ind = WaitForMultipleObjects(3, pool, FALSE, INFINITE);

        if (ind == 0) { 
            c_num = client_number;
            cout << "\n[Server] Client " << c_num << " started session.\n";
            client_number = (client_number + 1) % n;
        }
        else if (ind == 1) { 
            char buffer[1000] = {0};
            int r = recv(Sockets[c_num], buffer, sizeof(buffer), 0);
            if (!printCausedBy(r, "Recv")) {
                closesocket(Sockets[c_num]);
                active--;
                continue;
            }
            string req = buffer;
            
            if (req == "exit") {
                cout << "[Server] Client " << c_num << " disconnected.\n";
                closesocket(Sockets[c_num]);
                active--;
                continue;
            }

            cout << "[Server] Received from client " << c_num << ": " << req << endl;
            string ans = convertNumber(req);
            cout << "[Server] Converted: " << ans << endl;
            
            strcpy(buffer, ans.c_str());
            int s = send(Sockets[c_num], buffer, sizeof(buffer), 0);
            printCausedBy(s, "Send");
        }
        else if (ind == 2) {
            cout << "[Server] Client " << c_num << " ended session. Semaphore released.\n";
            ReleaseSemaphore(hSemaphore, 1, NULL);
        }
    }

    cout << "\nAll clients finished. Server shutting down.\n";
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    for (int i = 0; i < 3; ++i) CloseHandle(pool[i]);
    CloseHandle(hSemaphore);
    closesocket(sListen);
    WSACleanup();
    return 0;
}