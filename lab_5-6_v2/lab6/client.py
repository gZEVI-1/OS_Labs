import sys
import socket
import threading
import datetime
from pathlib import Path

# Импорт компонентов PySide6 для построения графического интерфейса
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QLineEdit, QTextEdit, QMessageBox, QFileDialog,
    QGroupBox, QComboBox
)
# Импорт базовых классов Qt: флаги выравнивания и механизм сигналов/слотов
from PySide6.QtCore import Qt, Signal, QObject


# -----------------------------------------------------------------------------
# Класс сигналов для безопасной передачи данных между рабочим потоком сокета
# и главным потоком GUI (Qt требует, чтобы виджеты обновлялись только из
# главного потока).
# -----------------------------------------------------------------------------
class ClientSignals(QObject):
    log_msg = Signal(str)          # Сигнал для добавления строки в лог
    connected = Signal()           # Сигнал успешного подключения к серверу
    disconnected = Signal()        # Сигнал отключения от серверу
    result_received = Signal(str)  # Сигнал получения результата от сервера
    session_started = Signal()     # Сигнал начала сеанса (семафор захвачен)
    session_ended = Signal()       # Сигнал завершения сеанса (семафор освобождён)


# -----------------------------------------------------------------------------
# Главное окно клиента. Управляет GUI, сетевым подключением и логированием.
# -----------------------------------------------------------------------------
class NumberConverterClient(QMainWindow):
    def __init__(self):
        super().__init__()
        # Заголовок окна и начальная геометрия (x, y, ширина, высота)
        self.setWindowTitle("Да кто сюда вообще смотрит?!")
        self.setGeometry(900, 100, 550, 650)

        # Сетевые атрибуты: объект сокета, поток приёма данных, флаги состояния
        self.sock = None
        self.recv_thread = None
        self.connected = False      # Флаг установленного TCP-соединения
        self.session_active = False # Флаг активного сеанса (START отправлен)

        # Настройка файлового логирования: текущий лог и каталог для архивации
        self.log_file = Path("client_log.txt")
        self.archive_dir = Path("prev_session")
        self.archive_dir.mkdir(exist_ok=True)  # Создать каталог, если отсутствует
        self._archive_old_log()                # Переместить старый лог в архив

        # Инициализация системы сигналов и привязка обработчиков (слотов)
        self.signals = ClientSignals()
        self.signals.log_msg.connect(self.append_log)
        self.signals.connected.connect(self.on_connected)
        self.signals.disconnected.connect(self.on_disconnected)
        self.signals.result_received.connect(self.on_result)
        self.signals.session_started.connect(self.on_session_started)
        self.signals.session_ended.connect(self.on_session_ended)

        # Построение интерфейса и запись стартового сообщения в журнал
        self.init_ui()
        self.log_event("Клиент инициализирован.")

    # -------------------------------------------------------------------------
    # Архивация предыдущего файла лога с меткой времени, чтобы не затирать
    # историю при новом запуске приложения.
    # -------------------------------------------------------------------------
    def _archive_old_log(self):
        if self.log_file.exists():
            ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            self.log_file.rename(self.archive_dir / f"client_log_{ts}.txt")

    # -------------------------------------------------------------------------
    # Создание и компоновка всех виджетов интерфейса.
    # -------------------------------------------------------------------------
    def init_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

        # --- Группа "Подключение к серверу" ---
        cg = QGroupBox("Подключение к серверу")
        ch = QHBoxLayout()
        ch.addWidget(QLabel("IP:"))
        self.txt_ip = QLineEdit("127.0.0.1")  # Поле ввода IP-адреса сервера
        self.txt_ip.setMaximumWidth(120)
        ch.addWidget(self.txt_ip)

        ch.addWidget(QLabel("Порт:"))
        self.txt_port = QLineEdit("1111")     # Поле ввода порта сервера
        self.txt_port.setMaximumWidth(60)
        ch.addWidget(self.txt_port)

        # Кнопка подключения: устанавливает TCP-соединение с сервером
        self.btn_conn = QPushButton("Подключиться")
        self.btn_conn.setStyleSheet("background-color:#2196F3;color:white;font-weight:bold;")
        self.btn_conn.clicked.connect(self.connect_to_server)

        # Кнопка отключения: разрывает соединение и освобождает ресурсы
        self.btn_disconn = QPushButton("Отключиться")
        self.btn_disconn.setStyleSheet("background-color:#f44336;color:white;")
        self.btn_disconn.clicked.connect(self.disconnect)
        self.btn_disconn.setEnabled(False)  # Недоступна до подключения

        ch.addWidget(self.btn_conn)
        ch.addWidget(self.btn_disconn)
        ch.addStretch()
        cg.setLayout(ch)
        layout.addWidget(cg)

        # --- Группа "Управление сеансом" ---
        # Сеанс — это период, в течение которого клиент владеет семафором
        # на сервере и может отправлять данные для конвертации.
        sg = QGroupBox("Управление сеансом")
        sh = QHBoxLayout()
        self.btn_start = QPushButton("Начать сеанс")
        self.btn_start.setEnabled(False)  # Доступна только после подключения
        self.btn_start.clicked.connect(self.start_session)

        self.btn_end = QPushButton("Завершить сеанс")
        self.btn_end.setEnabled(False)    # Доступна только во время активного сеанса
        self.btn_end.clicked.connect(self.end_session)

        # Метка для отображения текущего состояния сеанса
        self.lbl_sess = QLabel("Сеанс: неактивен")
        self.lbl_sess.setStyleSheet("color:#666;")

        sh.addWidget(self.btn_start)
        sh.addWidget(self.btn_end)
        sh.addStretch()
        sh.addWidget(self.lbl_sess)
        sg.setLayout(sh)
        layout.addWidget(sg)

        # --- Группа "Перевод числа" ---
        tg = QGroupBox("Перевод числа")
        tv = QVBoxLayout()
        th = QHBoxLayout()
        th.addWidget(QLabel("Число:"))
        self.txt_num = QLineEdit()
        self.txt_num.setPlaceholderText("255")  # Подсказка в пустом поле
        th.addWidget(self.txt_num)

        th.addWidget(QLabel("Система:"))
        self.cmb_sys = QComboBox()
        self.cmb_sys.addItems(["bin", "hex"])  # Доступные системы счисления
        th.addWidget(self.cmb_sys)
        tv.addLayout(th)

        # Кнопка отправки запроса на сервер (доступна только в активном сеансе)
        self.btn_send = QPushButton("Отправить на сервер")
        self.btn_send.setStyleSheet("background-color:#FF9800;color:white;font-weight:bold;")
        self.btn_send.setEnabled(False)
        self.btn_send.clicked.connect(self.send_request)
        tv.addWidget(self.btn_send)
        tg.setLayout(tv)
        layout.addWidget(tg)

        # --- Группа "Результат с сервера" ---
        rg = QGroupBox("Результат с сервера")
        rv = QVBoxLayout()
        self.lbl_res = QLabel("Ожидание данных...")
        self.lbl_res.setAlignment(Qt.AlignCenter)
        self.lbl_res.setMinimumHeight(60)
        # Тёмная тема оформления для поля результата
        self.lbl_res.setStyleSheet("""
            font-size:16px; color:#888; font-weight:bold;
            padding:10px; background-color:#1e1e1e; border-radius:5px;
        """)
        rv.addWidget(self.lbl_res)
        rg.setLayout(rv)
        layout.addWidget(rg)

        # --- Группа "Журнал событий" ---
        lg = QGroupBox("Журнал событий")
        lv = QVBoxLayout()
        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)  # Лог только для чтения
        # Моноширинный шрифт и тёмная цветовая схема
        self.txt_log.setStyleSheet("""
            QTextEdit {
                background-color:#1e1e1e; color:#d4d4d4;
                font-family:Consolas,monospace; font-size:11px;
            }
        """)
        lv.addWidget(self.txt_log)
        lg.setLayout(lv)
        layout.addWidget(lg, stretch=1)  # Растягивается при изменении размера окна

        # --- Нижняя панель: просмотр архива и сохранение лога ---
        lh = QHBoxLayout()
        self.btn_prev = QPushButton("Лог предыдущей сессии")
        self.btn_prev.clicked.connect(self.view_previous_log)
        self.btn_save = QPushButton("Сохранить лог")
        self.btn_save.clicked.connect(self.save_log)
        lh.addWidget(self.btn_prev)
        lh.addWidget(self.btn_save)
        layout.addLayout(lh)

    # -------------------------------------------------------------------------
    # Запись события в журнал: добавляет метку времени, отправляет сигнал в GUI
    # и дублирует строку в текстовый файл.
    # -------------------------------------------------------------------------
    def log_event(self, msg: str):
        ts = datetime.datetime.now().strftime("%H:%M:%S")
        line = f"[{ts}] {msg}"
        self.signals.log_msg.emit(line)  # Безопасное обновление GUI из любого потока
        with open(self.log_file, "a", encoding="utf-8") as f:
            f.write(line + "\n")

    # -------------------------------------------------------------------------
    # Слот для сигнала log_msg: добавляет строку в QTextEdit и прокручивает
    # область просмотра вниз, чтобы показать последнее сообщение.
    # -------------------------------------------------------------------------
    def append_log(self, line: str):
        self.txt_log.append(line)
        self.txt_log.verticalScrollBar().setValue(self.txt_log.verticalScrollBar().maximum())

    # -------------------------------------------------------------------------
    # Обработчик сигнала connected: обновляет состояние интерфейса после
    # успешного установления TCP-соединения с сервером.
    # -------------------------------------------------------------------------
    def on_connected(self):
        self.connected = True
        self.btn_conn.setEnabled(False)
        self.btn_disconn.setEnabled(True)
        self.btn_start.setEnabled(True)
        self.lbl_sess.setText("Подключён. Ожидание начала сеанса.")
        self.lbl_sess.setStyleSheet("color:#2196F3;")
        self.log_event("Подключение к серверу установлено.")

    # -------------------------------------------------------------------------
    # Обработчик сигнала disconnected: сбрасывает все флаги и возвращает
    # интерфейс в исходное состояние.
    # -------------------------------------------------------------------------
    def on_disconnected(self):
        self.connected = False
        self.session_active = False
        self.btn_conn.setEnabled(True)
        self.btn_disconn.setEnabled(False)
        self.btn_start.setEnabled(False)
        self.btn_end.setEnabled(False)
        self.btn_send.setEnabled(False)
        self.lbl_sess.setText("Сеанс: неактивен")
        self.lbl_sess.setStyleSheet("color:#666;")
        self.lbl_res.setText("Отключено от сервера")
        self.lbl_res.setStyleSheet("""
            font-size:16px; color:#f44336; font-weight:bold;
            padding:10px; background-color:#1e1e1e; border-radius:5px;
        """)
        self.log_event("Отключение выполнено.")

    # -------------------------------------------------------------------------
    # Обработчик сигнала session_started: вызывается, когда сервер подтвердил
    # захват семафора (OK: Session started). Активирует отправку данных.
    # -------------------------------------------------------------------------
    def on_session_started(self):
        self.session_active = True
        self.btn_start.setEnabled(False)
        self.btn_end.setEnabled(True)
        self.btn_send.setEnabled(True)
        self.lbl_sess.setText("Сеанс: АКТИВЕН (семафор захвачен)")
        self.lbl_sess.setStyleSheet("color:#4CAF50; font-weight: bold;")
        self.lbl_res.setText("Готов к передаче данных")
        self.lbl_res.setStyleSheet("""
            font-size:16px; color:#4CAF50; font-weight:bold;
            padding:10px; background-color:#1e1e1e; border-radius:5px;
        """)
        self.log_event("Сеанс начат. Можно отправлять данные.")

    # -------------------------------------------------------------------------
    # Обработчик сигнала session_ended: вызывается по команде END или при
    # принудительном завершении. Соединение остаётся открытым.
    # -------------------------------------------------------------------------
    def on_session_ended(self):
        self.session_active = False
        self.btn_start.setEnabled(True)
        self.btn_end.setEnabled(False)
        self.btn_send.setEnabled(False)
        self.lbl_sess.setText("Сеанс завершён. Можно начать новый.")
        self.lbl_sess.setStyleSheet("color:#FF9800;")
        self.lbl_res.setText("Сеанс завершён. Нажмите 'Начать сеанс' для продолжения.")
        self.lbl_res.setStyleSheet("""
            font-size:16px; color:#FF9800; font-weight:bold;
            padding:10px; background-color:#1e1e1e; border-radius:5px;
        """)
        self.log_event("Сеанс завершён. Соединение остаётся активным.")

    # -------------------------------------------------------------------------
    # Обработчик сигнала result_received: отображает результат конвертации,
    # полученный от сервера. Окрашивает текст в красный при ошибке.
    # -------------------------------------------------------------------------
    def on_result(self, res: str):
        self.lbl_res.setText(res)
        if res.startswith("ERROR"):
            self.lbl_res.setStyleSheet("""
                font-size:16px; color:#f44336; font-weight:bold;
                padding:10px; background-color:#1e1e1e; border-radius:5px;
            """)
        else:
            self.lbl_res.setStyleSheet("""
                font-size:16px; color:#4CAF50; font-weight:bold;
                padding:10px; background-color:#1e1e1e; border-radius:5px;
            """)
        self.log_event(f"Получен результат от сервера: {res}")

    # -------------------------------------------------------------------------
    # Установка TCP-соединения с сервером. Создаёт сокет, подключается к
    # указанному IP и порту, затем запускает фоновый поток для чтения
    # входящих сообщений (receive_loop).
    # -------------------------------------------------------------------------
    def connect_to_server(self):
        ip = self.txt_ip.text().strip()
        port = int(self.txt_port.text().strip())
        self.log_event(f"Попытка подключения к {ip}:{port}...")
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((ip, port))
            self.signals.connected.emit()
            # daemon=True — поток завершится вместе с основным процессом GUI
            self.recv_thread = threading.Thread(target=self.receive_loop, daemon=True)
            self.recv_thread.start()
        except Exception as e:
            QMessageBox.critical(self, "Ошибка подключения", str(e))
            self.log_event(f"Ошибка подключения: {e}")

    # -------------------------------------------------------------------------
    # Фоновый цикл приёма данных из сокета. Работает в отдельном потоке.
    # Анализирует префиксы сообщений сервера и эмитирует соответствующие
    # сигналы для обновления GUI в главном потоке.
    # -------------------------------------------------------------------------
    def receive_loop(self):
        while self.connected and self.sock:
            try:
                # Чтение до 1024 байт и декодирование из UTF-8
                data = self.sock.recv(1024).decode("utf-8").strip()
                if not data:
                    break  # Соединение закрыто сервером

                # Маршрутизация сообщений по префиксам
                if data.startswith("WAIT:"):
                    self.signals.log_msg.emit("Сервер занят другим клиентом. Ожидание освобождения...")
                elif data.startswith("OK: Session started"):
                    self.signals.session_started.emit()
                elif data.startswith("OK: Session ended"):
                    self.signals.session_ended.emit()
                elif data.startswith("ERROR:"):
                    self.signals.log_msg.emit(f"Ошибка сервера: {data}")
                else:
                    # Любое другое сообщение считается результатом конвертации
                    self.signals.result_received.emit(data)
            except Exception as e:
                if self.connected:
                    self.signals.log_msg.emit(f"Ошибка приёма: {e}")
                break
        # Если цикл завершился при активном соединении — считаем, что сервер
        # разорвал связь, и эмитируем сигнал отключения.
        if self.connected:
            self.signals.disconnected.emit()

    # -------------------------------------------------------------------------
    # Отправка серверу команды START для захвата семафора (начало сеанса).
    # -------------------------------------------------------------------------
    def start_session(self):
        if not self.connected:
            return
        self.log_event("Отправка START (запрос на захват семафора)...")
        try:
            self.sock.sendall(b"START")
        except Exception as e:
            QMessageBox.critical(self, "Ошибка", str(e))

    # -------------------------------------------------------------------------
    # Отправка серверу команды END для освобождения семафора (завершение
    # сеанса). Соединение при этом не разрывается.
    # -------------------------------------------------------------------------
    def end_session(self):
        if not self.connected:
            return
        self.log_event("Отправка END (освобождение семафора)...")
        try:
            self.sock.sendall(b"END")
        except Exception as e:
            QMessageBox.critical(self, "Ошибка", str(e))

    # -------------------------------------------------------------------------
    # Отправка данных для конвертации. Формат: "<число> <bin|hex>".
    # Доступно только при активном сеансе (session_active == True).
    # -------------------------------------------------------------------------
    def send_request(self):
        if not self.session_active or not self.sock:
            QMessageBox.warning(self, "Внимание", "Сначала начните сеанс!")
            return
        num = self.txt_num.text().strip()
        sys = self.cmb_sys.currentText()
        if not num:
            QMessageBox.warning(self, "Внимание", "Введите число!")
            return
        msg = f"{num} {sys}"
        self.log_event(f"Отправка данных: '{msg}'")
        try:
            self.sock.sendall(msg.encode("utf-8"))
            # Визуальная индикация ожидания ответа
            self.lbl_res.setText("Ожидание ответа...")
            self.lbl_res.setStyleSheet("""
                font-size:16px; color:#FF9800; font-weight:bold;
                padding:10px; background-color:#1e1e1e; border-radius:5px;
            """)
        except Exception as e:
            QMessageBox.critical(self, "Ошибка отправки", str(e))

    # -------------------------------------------------------------------------
    # Корректное отключение от сервера: отправка EXIT, закрытие сокета,
    # сброс флагов и обновление интерфейса.
    # -------------------------------------------------------------------------
    def disconnect(self):
        if self.sock:
            try:
                self.sock.sendall(b"EXIT")  # Уведомляем сервер о выходе
            except:
                pass
            try:
                self.sock.close()
            except:
                pass
            self.sock = None
        self.connected = False
        self.session_active = False
        self.signals.disconnected.emit()

    # -------------------------------------------------------------------------
    # Открытие диалога выбора архивного файла лога и его просмотр.
    # -------------------------------------------------------------------------
    def view_previous_log(self):
        fp, _ = QFileDialog.getOpenFileName(
            self, "Лог предыдущей сессии", str(self.archive_dir), "Text files (*.txt)"
        )
        if fp:
            try:
                with open(fp, "r", encoding="utf-8") as f:
                    content = f.read()
                # Создание модального диалога с возможностью выделения текста
                mb = QMessageBox(self)
                mb.setWindowTitle(Path(fp).name)
                mb.setTextInteractionFlags(Qt.TextSelectableByMouse | Qt.TextSelectableByKeyboard)
                preview = content[:3000] + ("..." if len(content) > 3000 else "")
                mb.setInformativeText(preview)
                mb.setDetailedText(content)  # Полный текст доступен через "Показать подробности"
                mb.setStandardButtons(QMessageBox.Ok)
                mb.setStyleSheet("QLabel{min-width: 500px;}")
                mb.exec()
            except Exception as e:
                QMessageBox.critical(self, "Ошибка", str(e))

    # -------------------------------------------------------------------------
    # Сохранение текущего файла лога в выбранное пользователем место.
    # -------------------------------------------------------------------------
    def save_log(self):
        fp, _ = QFileDialog.getSaveFileName(self, "Сохранить лог", "client_log.txt", "Text files (*.txt)")
        if fp:
            try:
                with open(self.log_file, "r", encoding="utf-8") as src, open(fp, "w", encoding="utf-8") as dst:
                    dst.write(src.read())
                QMessageBox.information(self, "Успех", f"Сохранено: {fp}")
            except Exception as e:
                QMessageBox.critical(self, "Ошибка", str(e))

    # -------------------------------------------------------------------------
    # Переопределение события закрытия окна: если соединение активно,
    # выполняем корректное отключение перед завершением приложения.
    # -------------------------------------------------------------------------
    def closeEvent(self, event):
        if self.connected:
            self.disconnect()
        event.accept()


# -----------------------------------------------------------------------------
# Точка входа: создание приложения Qt, применение тёмной темы и запуск
# главного цикла обработки событий.
# -----------------------------------------------------------------------------
if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    # Глобальная таблица стилей для тёмного оформления всех виджетов
    app.setStyleSheet("""
        QMainWindow{background-color:#2b2b2b;}
        QGroupBox{font-weight:bold;border:1px solid #555;border-radius:5px;margin-top:10px;padding-top:10px;color:#ddd;}
        QGroupBox::title{subcontrol-origin:margin;left:10px;}
        QLabel{color:#ddd;}
        QPushButton{padding:6px 12px;border-radius:4px;background-color:#3c3c3c;color:#ddd;border:1px solid #555;}
        QPushButton:hover{background-color:#4c4c4c;}
        QLineEdit,QComboBox{background-color:#3c3c3c;color:#ddd;border:1px solid #555;padding:4px;}
    """)
    w = NumberConverterClient()
    w.show()
    sys.exit(app.exec())