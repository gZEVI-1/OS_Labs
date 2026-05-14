#include <iostream>
#include <string>
#include <winsock2.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#pragma warning(disable: 4996)

using namespace std;

bool printCausedBy(int Result, const char* nameOfOper) {
    if (!Result) {
        cout << "Connection closed.\n";
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
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(1111);
    addr.sin_family = AF_INET;

    SOCKET Connection = socket(AF_INET, SOCK_STREAM, NULL);
    if (connect(Connection, (SOCKADDR*)&addr, sizeof(addr)) != 0) {
        cerr << "Unable to connect to server.\n";
        return 1;
    }

    HANDLE hSemaphore = OpenSemaphore(SYNCHRONIZE, FALSE, "Lab");
    if (hSemaphore == NULL) {
        cerr << "Error open semaphore.\n";
        return GetLastError();
    }

    HANDLE pool[3] = {
        OpenEvent(EVENT_ALL_ACCESS, FALSE, "Start"),
        OpenEvent(EVENT_ALL_ACCESS, FALSE, "Data"),
        OpenEvent(EVENT_ALL_ACCESS, FALSE, "End")
    };
    for (int i = 0; i < 3; ++i) {
        if (!pool[i]) {
            cerr << "OpenEvent error: " << GetLastError() << endl;
            return 1;
        }
    }

    cout << "Waiting for free slot on server...\n";
    WaitForSingleObject(hSemaphore, INFINITE);
    cout << "Slot acquired! Connection established.\n";

    while (true) {
        cout << "\nEnter <start> to begin session, <quit> to disconnect >> ";
        string cmd;
        cin >> cmd;
        while (cin.fail() || (cmd != "start" && cmd != "quit") || cin.peek() != '\n') {
            cin.clear();
            cin.ignore(32768, '\n');
            cout << "Invalid. Enter <start> or <quit> >> ";
            cin >> cmd;
        }

        if (cmd == "quit") {
            char buf[1000] = "exit";
            send(Connection, buf, sizeof(buf), 0);
            cout << "Disconnecting...\n";
            break;
        }

        SetEvent(pool[0]); 
        cout << "Session started. Commands: <convert> <end>\n";

        while (true) {
            cout << ">> ";
            cin >> cmd;
            while (cin.fail() || (cmd != "convert" && cmd != "end") || cin.peek() != '\n') {
                cin.clear();
                cin.ignore(32768, '\n');
                cout << "Invalid. Enter <convert> or <end> >> ";
                cin >> cmd;
            }

            if (cmd == "end") break;

            if (cmd == "convert") {
                cin.clear();
                cin.ignore(32768, '\n');
                cout << "Enter number and target system (bin|hex), e.g. 255 hex >> ";
                string data;
                getline(cin, data);

                SetEvent(pool[1]); 
                char buffer[1000] = {0};
                strcpy(buffer, data.c_str());
                if (!printCausedBy(send(Connection, buffer, sizeof(buffer), 0), "Send"))
                    break;

                ZeroMemory(buffer, sizeof(buffer));
                int r = recv(Connection, buffer, sizeof(buffer), 0);
                if (!printCausedBy(r, "Recv")) break;
                cout << "Server result: " << buffer << endl;
            }
        }

        SetEvent(pool[2]);
        cout << "Session ended. Slot released. Waiting for next slot...\n";
        WaitForSingleObject(hSemaphore, INFINITE);
        cout << "Slot acquired again! Ready for new session.\n";
    }

    for (int i = 0; i < 3; ++i) CloseHandle(pool[i]);
    CloseHandle(hSemaphore);
    closesocket(Connection);
    WSACleanup();
    return 0;
}