

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <winsock2.h>   // Windows Sockets API — сетевые функции
#include <windows.h>    // Windows API — события, семафоры, процессы

#pragma comment(lib, "ws2_32.lib")  // Автоматическая линковка библиотеки Winsock
#pragma warning(disable: 4996)      // Отключение предупреждений об устаревших функциях (inet_addr)

using namespace std;


// ============================================================================
// Функция convertNumber
// ----------------------------------------------------------------------------
// Преобразует десятичное число в двоичное или шестнадцатеричное представление.
//
// Параметры:
//   input — строка формата "<число> <bin|hex>"
//
// Возвращает:
//   Строку с результатом (например, "0b11111111", "0xFF") или сообщение об ошибке.
//
// Особенности:
//   • Для двоичной системы: число должно быть неотрицательным
//   • Для шестнадцатеричной: поддерживаются отрицательные числа
//   • Реализован ручной алгоритм деления/сдвига без использования std::bitset
// ============================================================================
string convertNumber(const string& input) {
    istringstream iss(input);   // Поток для разбора входной строки
    long long num;              // Целевое число (64-бит для большого диапазона)
    string type;                // Целевая система счисления (bin/hex)
    iss >> num >> type;         // Извлечение числа и типа из строки

    // Проверка корректности формата ввода
    if (iss.fail()) 
        return "Error: invalid format. Use: <number> <bin|hex>";

    string res;  // Результирующая строка (накапливается справа налево)

    // --- Конвертация в двоичную систему ---
    if (type == "bin" || type == "BIN") {
        // Двоичная система: отрицательные числа не поддерживаются
        if (num < 0) return "Error: negative number for bin";
        if (num == 0) return "0b0";  // Граничный случай

        // Побитовый сдвиг вправо и извлечение младшего бита через AND
        while (num > 0) {
            res = char('0' + (num & 1)) + res;  // num & 1 — получение младшего бита
            num >>= 1;                           // Сдвиг вправо (деление на 2)
        }
        return "0b" + res;  // Префикс двоичного числа
    } 
    // --- Конвертация в шестнадцатеричную систему ---
    else if (type == "hex" || type == "HEX") {
        if (num == 0) return "0x0";  // Граничный случай

        bool neg = (num < 0);        // Запоминаем знак
        if (neg) num = -num;         // Работаем с абсолютным значением

        // Последовательное деление на 16 с накоплением остатков
        while (num > 0) {
            int d = num % 16;        // Остаток от деления на 16
            // Преобразование остатка в символ 0-9 или A-F
            char c = (d < 10) ? ('0' + d) : ('A' + d - 10);
            res = char(c) + res;     // Дописываем цифру слева
            num /= 16;               // Целочисленное деление на 16
        }
        return (neg ? "-" : "") + string("0x") + res;  // Префикс и знак
    }

    // Неизвестный тип системы счисления
    return "Error: type must be 'bin' or 'hex'";
}


// ============================================================================
// Функция printCausedBy
// ----------------------------------------------------------------------------
// Проверяет результат сетевой операции и выводит диагностику при ошибке.
//
// Параметры:
//   Result      — код возврата операции (recv/send)
//   nameOfOper  — название операции для вывода в сообщении
//
// Возвращает:
//   true  — операция выполнена успешно
//   false — произошла ошибка или соединение закрыто
//
// Примечание:
//   Result == 0  → клиент корректно закрыл соединение (graceful shutdown)
//   Result < 0   → сетевая ошибка, код доступен через WSAGetLastError()
// ============================================================================
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


// ============================================================================
// Точка входа — main()
// ----------------------------------------------------------------------------
// Алгоритм работы сервера:
//   1. Инициализация Winsock (сетевой подсистемы Windows)
//   2. Создание слушающего сокета (bind + listen на 127.0.0.1:1111)
//   3. Создание объектов синхронизации (семафор + 3 события)
//   4. Запрос количества клиентов и запуск клиентских процессов
//   5. Приём подключений от всех клиентов
//   6. Главный цикл: ожидание событий от клиентов и обработка запросов
//   7. Освобождение ресурсов при завершении работы
// ============================================================================
int main() {
    // --- Шаг 1: Инициализация Winsock ---
    WSADATA wsaData;                    // Структура для информации о реализации Winsock
    WORD DLLVersion = MAKEWORD(2, 1);   // Запрос версии 2.1
    if (WSAStartup(DLLVersion, &wsaData) != 0) {
        cerr << "Error: failed to link library.\n";
        return 1;
    }

    // --- Шаг 2: Настройка адреса и создание слушающего сокета ---
    SOCKADDR_IN addr;                   // Структура адреса IPv4
    static int sizeOfAddr = sizeof(addr);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");  // Локальный интерфейс (loopback)
    addr.sin_port = htons(1111);                     // Порт 1111 (сетевой порядок байт)
    addr.sin_family = AF_INET;                       // Семейство адресов IPv4

    SOCKET sListen = socket(AF_INET, SOCK_STREAM, NULL);  // TCP-сокет (потоковый)
    if (bind(sListen, (SOCKADDR*)&addr, sizeOfAddr) == SOCKET_ERROR) {
        cerr << "Bind error: " << WSAGetLastError() << endl;
        closesocket(sListen);
        WSACleanup();
        return 1;
    }

    if (listen(sListen, SOMAXCONN) == SOCKET_ERROR) {  // SOMAXCONN — макс. длина очереди
        cerr << "Listen failed.\n";
        closesocket(sListen);
        WSACleanup();
        return 1;
    }

    // --- Шаг 3: Создание объектов синхронизации ---
    // Семафор "Lab": начальное значение 1, максимум 1 — бинарный семафор (мьютекс)
    // Гарантирует, что только один клиент одновременно находится в активной сессии
    HANDLE hSemaphore = CreateSemaphore(NULL, 1, 1, "Lab");
    if (hSemaphore == NULL) {
        cerr << "CreateSemaphore error: " << GetLastError() << endl;
        return 1;
    }

    // Массив именованных событий для сигнализации от клиентов к серверу:
    //   Start — клиент начал сессию
    //   Data  — клиент отправил данные для обработки
    //   End   — клиент завершил сессию (освобождает семафор)
    HANDLE pool[3] = {
        CreateEvent(NULL, FALSE, FALSE, "Start"),  // auto-reset, начально не сигнализировано
        CreateEvent(NULL, FALSE, FALSE, "Data"),   // auto-reset, начально не сигнализировано
        CreateEvent(NULL, FALSE, FALSE, "End")     // auto-reset, начально не сигнализировано
    };
    for (int i = 0; i < 3; ++i) {
        if (!pool[i]) {
            cerr << "CreateEvent error: " << GetLastError() << endl;
            return 1;
        }
    }

    // --- Шаг 4: Ввод количества клиентов и их запуск ---
    int n;
    cout << "Enter the number of clients (1 to 10) >> ";
    cin >> n;
    // Валидация ввода: целое число от 1 до 10, без лишних символов
    while (cin.fail() || n < 1 || n > 10 || cin.peek() != '\n') {
        cin.clear();                    // Сброс флагов ошибки
        cin.ignore(32768, '\n');        // Очистка буфера ввода
        cout << "Invalid input. Try again >> ";
        cin >> n;
    }

    // Структуры для CreateProcessW (запуск клиентских процессов)
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));        // Обнуление структуры
    si.cb = sizeof(si);               // Обязательное поле — размер структуры
    ZeroMemory(&pi, sizeof(pi));

    // Запуск n экземпляров Client.exe в отдельных консольных окнах
    for (int i = 0; i < n; i++) {
        wchar_t cmdLine[256];
        wcscpy_s(cmdLine, L"Client.exe");  // Имя исполняемого файла клиента
        if (!CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE,
            CREATE_NEW_CONSOLE,            // Каждый клиент в новом окне консоли
            NULL, NULL, &si, &pi)) {
            cerr << "CreateProcess failed: " << GetLastError() << endl;
            return 1;
        }
        Sleep(100);  // Небольшая пауза для стабильности запуска
    }

    // --- Шаг 5: Приём подключений от всех клиентов ---
    vector<SOCKET> Sockets(n);    // Вектор сокетов для каждого подключённого клиента
    size_t client_number = 0;     // Индекс текущего активного клиента (круговая очередь)
    size_t c_num = 0;             // Индекс клиента, с которым работаем прямо сейчас

    for (int i = 0; i < n; i++) {
        Sockets[i] = accept(sListen, (SOCKADDR*)&addr, &sizeOfAddr);
        if (Sockets[i] == INVALID_SOCKET) {
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            return 1;
        }
        cout << "Client " << i << " connected.\n";
    }

    // --- Шаг 6: Главный цикл обработки событий ---
    int active = n;  // Счётчик активных клиентов
    while (active > 0) {
        // Ожидание ЛЮБОГО из трёх событий (Start, Data, End)
        // FALSE = wait-any (срабатывает при первом поступившем сигнале)
        // INFINITE = ждать бесконечно
        int ind = WaitForMultipleObjects(3, pool, FALSE, INFINITE);

        // --- Событие "Start" (индекс 0): клиент начал новую сессию ---
        if (ind == 0) { 
            c_num = client_number;  // Запоминаем, какой клиент начал сессию
            cout << "\n[Server] Client " << c_num << " started session.\n";
            client_number = (client_number + 1) % n;  // Круговая очередь клиентов
        }
        // --- Событие "Data" (индекс 1): клиент отправил данные ---
        else if (ind == 1) { 
            char buffer[1000] = {0};  // Буфер для приёма данных
            int r = recv(Sockets[c_num], buffer, sizeof(buffer), 0);
            if (!printCausedBy(r, "Recv")) {  // Проверка на ошибку/закрытие
                closesocket(Sockets[c_num]);
                active--;
                continue;
            }
            string req = buffer;  // Преобразование C-строки в std::string

            // Клиент запросил отключение
            if (req == "exit") {
                cout << "[Server] Client " << c_num << " disconnected.\n";
                closesocket(Sockets[c_num]);
                active--;
                continue;
            }

            cout << "[Server] Received from client " << c_num << ": " << req << endl;

            // Обработка запроса и отправка результата
            string ans = convertNumber(req);
            cout << "[Server] Converted: " << ans << endl;

            strcpy(buffer, ans.c_str());  // Копирование результата в буфер
            int s = send(Sockets[c_num], buffer, sizeof(buffer), 0);
            printCausedBy(s, "Send");  // Проверка отправки
        }
        // --- Событие "End" (индекс 2): клиент завершил сессию ---
        else if (ind == 2) {
            cout << "[Server] Client " << c_num << " ended session. Semaphore released.\n";
            ReleaseSemaphore(hSemaphore, 1, NULL);  // Освобождение слота для других клиентов
        }
    }

    // --- Шаг 7: Завершение работы и освобождение ресурсов ---
    cout << "\nAll clients finished. Server shutting down.\n";
    CloseHandle(pi.hProcess);   // Закрытие handle процесса (последнего созданного)
    CloseHandle(pi.hThread);    // Закрытие handle главного потока
    for (int i = 0; i < 3; ++i) CloseHandle(pool[i]);  // Закрытие событий
    CloseHandle(hSemaphore);    // Закрытие семафора
    closesocket(sListen);       // Закрытие слушающего сокета
    WSACleanup();               // Деинициализация Winsock
    return 0;
}