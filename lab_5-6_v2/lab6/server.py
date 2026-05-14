import sys
import socket
import threading
import datetime
from pathlib import Path

# Импорт компонентов PySide6 для построения графического интерфейса сервера
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QTextEdit, QMessageBox, QFileDialog,
    QGroupBox, QSpinBox, QListWidget
)
from PySide6.QtCore import Qt, Signal, QObject


# -----------------------------------------------------------------------------
# Класс сигналов для безопасной передачи событий из рабочих потоков
# (обработчиков клиентов) в главный поток GUI.
# -----------------------------------------------------------------------------
class ServerSignals(QObject):
    new_log = Signal(str)              # Новая строка в журнал событий
    client_connected = Signal(str)     # Клиент подключился (addr)
    client_disconnected = Signal(str)  # Клиент отключился (addr)
    session_started = Signal(str)      # Сеанс начат (addr владельца семафора)
    session_ended = Signal(str)        # Сеанс завершён (addr)


# -----------------------------------------------------------------------------
# Главное окно сервера. Управляет GUI, прослушиванием порта, семафором
# и обработкой клиентских подключений.
# -----------------------------------------------------------------------------
class NumberConverterServer(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ЛР №6")
        self.setGeometry(100, 100, 750, 650)

        # Сетевые атрибуты
        self.server_socket = None  # Основной слушающий сокет
        self.running = False       # Флаг работы сервера (управляет циклами)

        # Семафор с ограничением 1: гарантирует, что только один клиент
        # одновременно может владеть активным сеансом (конвертировать числа).
        self.semaphore = threading.Semaphore(1)
        self.active_session_addr = None  # Адрес клиента, захватившего семафор

        # Словари для учёта клиентов: потоки обработки и сокеты
        self.clients = {}      # addr -> Thread
        self.client_socks = {} # addr -> socket

        # Настройка логирования с архивацией предыдущей сессии
        self.log_file = Path("server_log.txt")
        self.archive_dir = Path("prev_session")
        self.archive_dir.mkdir(exist_ok=True)
        self._archive_old_log()

        # Инициализация сигналов и привязка слотов
        self.signals = ServerSignals()
        self.signals.new_log.connect(self.append_log)
        self.signals.client_connected.connect(self.on_client_connected)
        self.signals.client_disconnected.connect(self.on_client_disconnected)
        self.signals.session_started.connect(self.on_session_started)
        self.signals.session_ended.connect(self.on_session_ended)

        self.init_ui()
        self.log_event("Сервер инициализирован. Готов к запуску.")

    # -------------------------------------------------------------------------
    # Перемещение старого файла лога в архив с меткой времени.
    # -------------------------------------------------------------------------
    def _archive_old_log(self):
        if self.log_file.exists():
            ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            self.log_file.rename(self.archive_dir / f"server_log_{ts}.txt")

    # -------------------------------------------------------------------------
    # Создание и компоновка виджетов интерфейса сервера.
    # -------------------------------------------------------------------------
    def init_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        # --- Группа управления сервером ---
        ctrl = QGroupBox("Управление сервером")
        h = QHBoxLayout()
        self.btn_start = QPushButton("Запустить сервер")
        self.btn_start.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
        self.btn_start.clicked.connect(self.start_server)

        self.btn_stop = QPushButton("Остановить сервер")
        self.btn_stop.setStyleSheet("background-color: #f44336; color: white; font-weight: bold;")
        self.btn_stop.clicked.connect(self.stop_server)
        self.btn_stop.setEnabled(False)  # Недоступен до запуска

        self.btn_prev = QPushButton("Лог предыдущей сессии")
        self.btn_prev.clicked.connect(self.view_previous_log)

        h.addWidget(self.btn_start)
        h.addWidget(self.btn_stop)
        h.addStretch()
        h.addWidget(self.btn_prev)
        ctrl.setLayout(h)
        layout.addWidget(ctrl)

        # --- Группа настроек ---
        sett = QGroupBox("Настройки")
        sh = QHBoxLayout()
        sh.addWidget(QLabel("Порт:"))
        self.spin_port = QSpinBox()
        self.spin_port.setRange(1024, 65535)  # Диапазон динамических портов
        self.spin_port.setValue(1111)
        sh.addWidget(self.spin_port)
        sh.addStretch()
        sett.setLayout(sh)
        layout.addWidget(sett)

        # --- Группа текущего статуса ---
        stat = QGroupBox("Текущий статус")
        sv = QVBoxLayout()
        self.lbl_status = QLabel("Сервер остановлен")
        self.lbl_status.setStyleSheet("font-size: 14px; color: #666;")
        self.lbl_active = QLabel("Активный сеанс: нет")
        self.lbl_active.setStyleSheet("color: #666;")
        sv.addWidget(self.lbl_status)
        sv.addWidget(self.lbl_active)
        stat.setLayout(sv)
        layout.addWidget(stat)

        # --- Группа подключённых клиентов ---
        cl = QGroupBox("Подключённые клиенты (ожидают или работают)")
        cv = QVBoxLayout()
        self.lst_clients = QListWidget()  # Визуальный список адресов клиентов
        cv.addWidget(self.lst_clients)
        cl.setLayout(cv)

        # --- Группа журнала событий ---
        log_box = QGroupBox("Журнал событий (текущая сессия)")
        lv = QVBoxLayout()
        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setStyleSheet("""
            QTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
                font-family: Consolas, monospace;
                font-size: 11px;
            }
        """)
        lv.addWidget(self.txt_log)
        log_box.setLayout(lv)
        layout.addWidget(log_box, stretch=1)

        # --- Нижняя панель ---
        lh = QHBoxLayout()
        self.btn_clear = QPushButton("Очистить отображение")
        self.btn_clear.clicked.connect(self.txt_log.clear)
        self.btn_save = QPushButton("Сохранить лог как")
        self.btn_save.clicked.connect(self.save_log)
        lh.addWidget(self.btn_clear)
        lh.addWidget(self.btn_save)
        layout.addLayout(lh)

    # -------------------------------------------------------------------------
    # Запись события в журнал с меткой времени. Дублирует в файл и GUI.
    # -------------------------------------------------------------------------
    def log_event(self, msg: str):
        ts = datetime.datetime.now().strftime("%H:%M:%S")
        line = f"[{ts}] {msg}"
        self.signals.new_log.emit(line)
        with open(self.log_file, "a", encoding="utf-8") as f:
            f.write(line + "\n")

    # -------------------------------------------------------------------------
    # Слот для добавления строки в QTextEdit с автопрокруткой вниз.
    # -------------------------------------------------------------------------
    def append_log(self, line: str):
        self.txt_log.append(line)
        self.txt_log.verticalScrollBar().setValue(self.txt_log.verticalScrollBar().maximum())

    # -------------------------------------------------------------------------
    # Слот: добавление адреса клиента в список подключённых.
    # -------------------------------------------------------------------------
    def on_client_connected(self, addr: str):
        self.lst_clients.addItem(addr)

    # -------------------------------------------------------------------------
    # Слот: удаление адреса клиента из списка при отключении.
    # -------------------------------------------------------------------------
    def on_client_disconnected(self, addr: str):
        for item in self.lst_clients.findItems(addr, Qt.MatchExactly):
            self.lst_clients.takeItem(self.lst_clients.row(item))

    # -------------------------------------------------------------------------
    # Слот: отображение адреса клиента, захватившего активный сеанс.
    # -------------------------------------------------------------------------
    def on_session_started(self, addr: str):
        self.lbl_active.setText(f"Активный сеанс: {addr}")
        self.lbl_active.setStyleSheet("color: #4CAF50; font-weight: bold;")

    # -------------------------------------------------------------------------
    # Слот: сброс информации об активном сеансе.
    # -------------------------------------------------------------------------
    def on_session_ended(self, addr: str):
        self.lbl_active.setText("Активный сеанс: нет")
        self.lbl_active.setStyleSheet("color: #666;")

    # -------------------------------------------------------------------------
    # Запуск сервера: создание слушающего сокета, привязка к 0.0.0.0:порт,
    # установка опции SO_REUSEADDR и запуск фонового потока accept_loop.
    # -------------------------------------------------------------------------
    def start_server(self):
        port = self.spin_port.value()
        self.running = True
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind(("0.0.0.0", port))
        self.server_socket.listen(10)  # Длина очереди ожидающих подключений

        self.btn_start.setEnabled(False)
        self.btn_stop.setEnabled(True)
        self.lbl_status.setText(f"Работает на порту {port}")
        self.lbl_status.setStyleSheet("font-size: 14px; color: #4CAF50; font-weight: bold;")
        self.log_event(f"СЕРВЕР ЗАПУЩЕН на порту {port}. Ожидание подключений...")

        threading.Thread(target=self.accept_loop, daemon=True).start()

    # -------------------------------------------------------------------------
    # Фоновый цикл приёма входящих TCP-подключений. Работает в отдельном
    # потоке. Для каждого клиента создаётся свой поток handle_client.
    # -------------------------------------------------------------------------
    def accept_loop(self):
        while self.running:
            try:
                # Таймаут 1 секунда позволяет периодически проверять флаг running
                self.server_socket.settimeout(1.0)
                sock, addr = self.server_socket.accept()
                addr_str = f"{addr[0]}:{addr[1]}"
                if not self.running:
                    sock.close()
                    break

                # Регистрация клиента и запуск обработчика
                self.client_socks[addr_str] = sock
                self.signals.client_connected.emit(addr_str)
                self.log_event(f"Принято подключение от {addr_str}")

                t = threading.Thread(target=self.handle_client, args=(sock, addr_str), daemon=True)
                self.clients[addr_str] = t
                t.start()

            except socket.timeout:
                continue  # Нормальное поведение: проверить флаг running и ждать дальше
            except Exception as e:
                if self.running:
                    self.log_event(f"Ошибка accept: {e}")

    # -------------------------------------------------------------------------
    # Обработчик клиента: работает в отдельном потоке для каждого подключения.
    # Разбирает команды START, END, EXIT и данные для конвертации.
    # Управляет семафором, ограничивающим число активных сеансов до одного.
    # -------------------------------------------------------------------------
    def handle_client(self, sock: socket.socket, addr: str):
        has_session = False  # Локальный флаг: захвачен ли семафор этим клиентом
        try:
            while self.running:
                data = sock.recv(1024).decode("utf-8").strip()
                if not data:
                    break  # Клиент закрыл соединение

                self.log_event(f"От {addr}: '{data}'")

                # --- Команда START: захват семафора ---
                if data == "START":
                    if has_session:
                        sock.sendall(b"ERROR: Session already active.")
                        continue

                    # Попытка неблокирующего захвата семафора.
                    # Если семафор занят, отправляем WAIT и блокируем поток
                    # до освобождения ресурса.
                    if not self.semaphore.acquire(blocking=False):
                        sock.sendall(b"WAIT: Server busy. Waiting for free slot...")
                        self.semaphore.acquire()  # Блокирующее ожидание
                    has_session = True
                    self.active_session_addr = addr
                    self.log_event(f"СЕАНС НАЧАТ: {addr} (семафор захвачен)")
                    sock.sendall(b"OK: Session started. Send <number> <bin|hex>")
                    self.signals.session_started.emit(addr)

                # --- Команда END: освобождение семафора ---
                elif data == "END":
                    if not has_session:
                        sock.sendall(b"ERROR: No active session.")
                    else:
                        self.semaphore.release()
                        has_session = False
                        self.active_session_addr = None
                        self.log_event(f"СЕАНС ЗАВЕРШЁН: {addr} (семафор освобождён, соединение активно)")
                        sock.sendall(b"OK: Session ended. Connection remains active.")
                        self.signals.session_ended.emit(addr)

                # --- Команда EXIT: полное отключение клиента ---
                elif data == "EXIT":
                    self.log_event(f"Клиент {addr} запросил полное отключение (EXIT)")
                    break

                # --- Данные для конвертации ---
                else:
                    # Без активного сеанса конвертация запрещена
                    if not has_session:
                        sock.sendall(b"ERROR: No active session. Send START first.")
                    else:
                        result = self.convert_number(data)
                        self.log_event(f"Результат для {addr}: {result}")
                        sock.sendall(result.encode("utf-8"))

        except Exception as e:
            if self.running:
                self.log_event(f"Клиент {addr} ошибка/отключение: {e}")
        finally:
            # Гарантированное освобождение семафора при аварийном отключении
            if has_session:
                self.semaphore.release()
                self.active_session_addr = None
                self.signals.session_ended.emit(addr)
            # Удаление клиента из учётных структур и закрытие сокета
            self.clients.pop(addr, None)
            self.client_socks.pop(addr, None)
            try:
                sock.close()
            except:
                pass
            self.signals.client_disconnected.emit(addr)
            self.log_event(f"Клиент {addr} отключён.")

    # -------------------------------------------------------------------------
    # Логика конвертации числа в указанную систему счисления.
    # Поддерживаются режимы "bin" (двоичная) и "hex" (шестнадцатеричная).
    # -------------------------------------------------------------------------
    def convert_number(self, data: str) -> str:
        parts = data.split()
        if len(parts) != 2:
            return "ERROR: Format '<number> <bin|hex>'"
        num_str, mode = parts
        try:
            num = int(num_str)
        except ValueError:
            return "ERROR: Invalid number"

        if mode.lower() == "bin":
            if num < 0:
                return "ERROR: Negative numbers not supported for bin"
            # Убираем префикс '0b' у результата bin(); для 0 возвращаем "0"
            return bin(num)[2:] if num != 0 else "0"
        elif mode.lower() == "hex":
            # Сохраняем знак минуса отдельно, затем переводим модуль в hex
            prefix = "-" if num < 0 else ""
            return prefix + hex(abs(num))[2:].upper()
        return "ERROR: Mode must be 'bin' or 'hex'"

    # -------------------------------------------------------------------------
    # Остановка сервера: сброс флага running, закрытие всех клиентских
    # сокетов и основного слушающего сокета. Обновление интерфейса.
    # -------------------------------------------------------------------------
    def stop_server(self):
        self.running = False
        self.log_event("Остановка сервера...")
        # Принудительное закрытие всех активных соединений для прерывания
        # блокирующих операций recv в потоках handle_client.
        for addr, sock in list(self.client_socks.items()):
            try:
                sock.close()
            except:
                pass
        if self.server_socket:
            try:
                self.server_socket.close()
            except:
                pass

        self.btn_start.setEnabled(True)
        self.btn_stop.setEnabled(False)
        self.lbl_status.setText("Сервер остановлен")
        self.lbl_status.setStyleSheet("font-size: 14px; color: #666;")
        self.lbl_active.setText("Активный сеанс: нет")
        self.lst_clients.clear()
        self.log_event("Сервер остановлен.")

    # -------------------------------------------------------------------------
    # Просмотр архивного файла лога через диалог выбора файла.
    # -------------------------------------------------------------------------
    def view_previous_log(self):
        fp, _ = QFileDialog.getOpenFileName(
            self, "Лог предыдущей сессии", str(self.archive_dir), "Text files (*.txt)"
        )
        if fp:
            try:
                with open(fp, "r", encoding="utf-8") as f:
                    content = f.read()
                mb = QMessageBox(self)
                mb.setWindowTitle(Path(fp).name)
                mb.setTextInteractionFlags(Qt.TextSelectableByMouse | Qt.TextSelectableByKeyboard)
                preview = content[:3000] + ("..." if len(content) > 3000 else "")
                mb.setInformativeText(preview)
                mb.setDetailedText(content)
                mb.setStandardButtons(QMessageBox.Ok)
                mb.setStyleSheet("QLabel{min-width: 500px;}")
                mb.exec()
            except Exception as e:
                QMessageBox.critical(self, "Ошибка", str(e))

    # -------------------------------------------------------------------------
    # Сохранение текущего журнала в выбранный пользователем файл.
    # -------------------------------------------------------------------------
    def save_log(self):
        fp, _ = QFileDialog.getSaveFileName(self, "Сохранить лог", "server_log.txt", "Text files (*.txt)")
        if fp:
            try:
                with open(self.log_file, "r", encoding="utf-8") as src, open(fp, "w", encoding="utf-8") as dst:
                    dst.write(src.read())
                QMessageBox.information(self, "Успех", f"Сохранено: {fp}")
            except Exception as e:
                QMessageBox.critical(self, "Ошибка", str(e))


# -----------------------------------------------------------------------------
# Точка входа: создание приложения Qt, применение тёмной темы и запуск
# главного цикла обработки событий.
# -----------------------------------------------------------------------------
if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.setStyleSheet("""
        QMainWindow{background-color:#2b2b2b;}
        QGroupBox{font-weight:bold;border:1px solid #555;border-radius:5px;margin-top:10px;padding-top:10px;color:#ddd;}
        QGroupBox::title{subcontrol-origin:margin;left:10px;}
        QLabel{color:#ddd;}
        QPushButton{padding:6px 12px;border-radius:4px;background-color:#3c3c3c;color:#ddd;border:1px solid #555;}
        QPushButton:hover{background-color:#4c4c4c;}
        QSpinBox,QLineEdit,QListWidget{background-color:#3c3c3c;color:#ddd;border:1px solid #555;padding:4px;}
        QListWidget::item:selected{background-color:#4CAF50;}
    """)
    w = NumberConverterServer()
    w.show()
    sys.exit(app.exec())