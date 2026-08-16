// ============================================================================
// CLIENT5.CPP — TCP-клиент с синхронизацией через события и семафор
// ============================================================================
// Назначение:
//   Клиент подключается к серверу, получает доступ к сессии через семафор,
//   отправляет запросы на конвертацию чисел и получает результаты.
//   Взаимодействие с сервером синхронизируется через именованные события.
//
// Архитектура синхронизации:
//   • Семафор "Lab"       — запрашивается перед сессией, освобождается после
//   • Событие "Start"     — сигнализирует серверу о начале сессии
//   • Событие "Data"      — сигнализирует серверу о готовности данных
//   • Событие "End"       — сигнализирует серверу о завершении сессии
//
// Жизненный цикл клиента:
//   1. Подключение к серверу
//   2. Ожидание семафора (получение слота)
//   3. Цикл сессий:
//      a. Команда "start" → сигнал Start → цикл обработки команд
//      b. Команда "convert" → ввод данных → сигнал Data → отправка → приём ответа
//      c. Команда "end" → выход из цикла обработки
//      d. Сигнал End → освобождение семафора → ожидание нового слота
//   4. Команда "quit" → отправка "exit" → отключение
// ============================================================================

#include <iostream>
#include <string>
#include <winsock2.h>   // Windows Sockets API
#include <windows.h>    // Windows API — события, семафоры

#pragma comment(lib, "ws2_32.lib")  // Автолинковка Winsock
#pragma warning(disable: 4996)      // Отключение предупреждений об устаревших функциях

using namespace std;


// ============================================================================
// Функция printCausedBy
// ----------------------------------------------------------------------------
// Проверяет результат сетевой операции и выводит диагностику.
//
// Параметры:
//   Result      — код возврата операции recv/send
//   nameOfOper  — название операции для сообщения об ошибке
//
// Возвращает:
//   true  — операция успешна
//   false — ошибка или соединение закрыто
// ============================================================================
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


// ============================================================================
// Точка входа — main()
// ----------------------------------------------------------------------------
// Алгоритм работы клиента:
//   1. Инициализация Winsock
//   2. Подключение к серверу (127.0.0.1:1111)
//   3. Открытие объектов синхронизации (семафор + 3 события)
//   4. Ожидание семафора (получение права на сессию)
//   5. Главный цикл: команды start/quit
//   6. Внутренний цикл сессии: команды convert/end
//   7. Освобождение ресурсов
// ============================================================================
int main() {
    // --- Шаг 1: Инициализация Winsock ---
    WSADATA wsaData;
    WORD DLLVersion = MAKEWORD(2, 1);
    if (WSAStartup(DLLVersion, &wsaData) != 0) {
        cerr << "Error: failed to link library.\n";
        return 1;
    }

    // --- Шаг 2: Настройка адреса сервера и подключение ---
    SOCKADDR_IN addr;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");  // Локальный сервер
    addr.sin_port = htons(1111);                     // Порт сервера
    addr.sin_family = AF_INET;                       // IPv4

    SOCKET Connection = socket(AF_INET, SOCK_STREAM, NULL);
    if (connect(Connection, (SOCKADDR*)&addr, sizeof(addr)) != 0) {
        cerr << "Unable to connect to server.\n";
        return 1;
    }

    // --- Шаг 3: Открытие объектов синхронизации (созданы сервером) ---
    // Открытие семафора "Lab" с правом синхронизации (ожидание)
    HANDLE hSemaphore = OpenSemaphore(SYNCHRONIZE, FALSE, "Lab");
    if (hSemaphore == NULL) {
        cerr << "Error open semaphore.\n";
        return GetLastError();
    }

    // Открытие трёх именованных событий с полным доступом
    HANDLE pool[3] = {
        OpenEvent(EVENT_ALL_ACCESS, FALSE, "Start"),  // Событие начала сессии
        OpenEvent(EVENT_ALL_ACCESS, FALSE, "Data"),   // Событие готовности данных
        OpenEvent(EVENT_ALL_ACCESS, FALSE, "End")     // Событие завершения сессии
    };
    for (int i = 0; i < 3; ++i) {
        if (!pool[i]) {
            cerr << "OpenEvent error: " << GetLastError() << endl;
            return 1;
        }
    }

    // --- Шаг 4: Первоначальное ожидание семафора ---
    cout << "Waiting for free slot on server...\n";
    WaitForSingleObject(hSemaphore, INFINITE);  // Блокировка до освобождения слота
    cout << "Slot acquired! Connection established.\n";

    // --- Шаг 5: Главный цикл клиента ---
    while (true) {
        // Запрос команды: start (начать сессию) или quit (отключиться)
        cout << "\nEnter <start> to begin session, <quit> to disconnect >> ";
        string cmd;
        cin >> cmd;
        // Валидация: только "start" или "quit", без лишних символов
        while (cin.fail() || (cmd != "start" && cmd != "quit") || cin.peek() != '\n') {
            cin.clear();
            cin.ignore(32768, '\n');
            cout << "Invalid. Enter <start> or <quit> >> ";
            cin >> cmd;
        }

        // --- Команда "quit": отключение от сервера ---
        if (cmd == "quit") {
            char buf[1000] = "exit";  // Специальное сообщение для сервера
            send(Connection, buf, sizeof(buf), 0);
            cout << "Disconnecting...\n";
            break;  // Выход из главного цикла
        }

        // --- Команда "start": начало новой сессии ---
        SetEvent(pool[0]);  // Сигнализируем серверу через событие "Start"
        cout << "Session started. Commands: <convert> <end>\n";

        // --- Внутренний цикл сессии ---
        while (true) {
            cout << ">> ";
            cin >> cmd;
            // Валидация: только "convert" или "end"
            while (cin.fail() || (cmd != "convert" && cmd != "end") || cin.peek() != '\n') {
                cin.clear();
                cin.ignore(32768, '\n');
                cout << "Invalid. Enter <convert> or <end> >> ";
                cin >> cmd;
            }

            // Команда "end": завершение текущей сессии
            if (cmd == "end") break;

            // Команда "convert": отправка данных на обработку
            if (cmd == "convert") {
                // Очистка буфера ввода перед getline
                cin.clear();
                cin.ignore(32768, '\n');
                cout << "Enter number and target system (bin|hex), e.g. 255 hex >> ";
                string data;
                getline(cin, data);  // Чтение всей строки (число + система)

                SetEvent(pool[1]);  // Сигнализируем серверу через событие "Data"

                // Отправка данных на сервер
                char buffer[1000] = {0};
                strcpy(buffer, data.c_str());
                if (!printCausedBy(send(Connection, buffer, sizeof(buffer), 0), "Send"))
                    break;  // При ошибке — прервать сессию

                // Получение результата от сервера
                ZeroMemory(buffer, sizeof(buffer));  // Обнуление буфера
                int r = recv(Connection, buffer, sizeof(buffer), 0);
                if (!printCausedBy(r, "Recv")) break;  // При ошибке — прервать сессию
                cout << "Server result: " << buffer << endl;
            }
        }

        // --- Завершение сессии ---
        SetEvent(pool[2]);  // Сигнализируем серверу через событие "End"
        cout << "Session ended. Slot released. Waiting for next slot...\n";

        // Ожидание нового слота (семафор) для следующей сессии
        WaitForSingleObject(hSemaphore, INFINITE);
        cout << "Slot acquired again! Ready for new session.\n";
    }

    // --- Шаг 6: Освобождение ресурсов ---
    for (int i = 0; i < 3; ++i) CloseHandle(pool[i]);  // Закрытие событий
    CloseHandle(hSemaphore);    // Закрытие семафора
    closesocket(Connection);    // Закрытие сокета
    WSACleanup();               // Деинициализация Winsock
    return 0;
}