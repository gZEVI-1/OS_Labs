import sys
import socket
import threading
import datetime
from pathlib import Path

from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QLineEdit, QTextEdit, QMessageBox, QFileDialog,
    QGroupBox, QComboBox
)
from PySide6.QtCore import Qt, Signal, QObject


class ClientSignals(QObject):
    log_msg = Signal(str)
    connected = Signal()
    disconnected = Signal()
    result_received = Signal(str)
    session_started = Signal()
    session_ended = Signal()


class NumberConverterClient(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Да кто сюда вообще смотрит?!")
        self.setGeometry(900, 100, 550, 650)

        self.sock = None
        self.recv_thread = None
        self.connected = False
        self.session_active = False

        self.log_file = Path("client_log.txt")
        self.archive_dir = Path("prev_session")
        self.archive_dir.mkdir(exist_ok=True)
        self._archive_old_log()

        self.signals = ClientSignals()
        self.signals.log_msg.connect(self.append_log)
        self.signals.connected.connect(self.on_connected)
        self.signals.disconnected.connect(self.on_disconnected)
        self.signals.result_received.connect(self.on_result)
        self.signals.session_started.connect(self.on_session_started)
        self.signals.session_ended.connect(self.on_session_ended)

        self.init_ui()
        self.log_event("Клиент инициализирован.")

    def _archive_old_log(self):
        if self.log_file.exists():
            ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            self.log_file.rename(self.archive_dir / f"client_log_{ts}.txt")

    def init_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)

   
        cg = QGroupBox("Подключение к серверу")
        ch = QHBoxLayout()
        ch.addWidget(QLabel("IP:"))
        self.txt_ip = QLineEdit("127.0.0.1")
        self.txt_ip.setMaximumWidth(120)
        ch.addWidget(self.txt_ip)
        ch.addWidget(QLabel("Порт:"))
        self.txt_port = QLineEdit("1111")
        self.txt_port.setMaximumWidth(60)
        ch.addWidget(self.txt_port)

        self.btn_conn = QPushButton("Подключиться")
        self.btn_conn.setStyleSheet("background-color:#2196F3;color:white;font-weight:bold;")
        self.btn_conn.clicked.connect(self.connect_to_server)

        self.btn_disconn = QPushButton("Отключиться")
        self.btn_disconn.setStyleSheet("background-color:#f44336;color:white;")
        self.btn_disconn.clicked.connect(self.disconnect)
        self.btn_disconn.setEnabled(False)

        ch.addWidget(self.btn_conn)
        ch.addWidget(self.btn_disconn)
        ch.addStretch()
        cg.setLayout(ch)
        layout.addWidget(cg)

        sg = QGroupBox("Управление сеансом")
        sh = QHBoxLayout()
        self.btn_start = QPushButton("Начать сеанс")
        self.btn_start.setEnabled(False)
        self.btn_start.clicked.connect(self.start_session)

        self.btn_end = QPushButton("Завершить сеанс")
        self.btn_end.setEnabled(False)
        self.btn_end.clicked.connect(self.end_session)

        self.lbl_sess = QLabel("Сеанс: неактивен")
        self.lbl_sess.setStyleSheet("color:#666;")
        sh.addWidget(self.btn_start)
        sh.addWidget(self.btn_end)
        sh.addStretch()
        sh.addWidget(self.lbl_sess)
        sg.setLayout(sh)
        layout.addWidget(sg)


        tg = QGroupBox("Перевод числа")
        tv = QVBoxLayout()
        th = QHBoxLayout()
        th.addWidget(QLabel("Число:"))
        self.txt_num = QLineEdit()
        self.txt_num.setPlaceholderText("255")
        th.addWidget(self.txt_num)
        th.addWidget(QLabel("Система:"))
        self.cmb_sys = QComboBox()
        self.cmb_sys.addItems(["bin", "hex"])
        th.addWidget(self.cmb_sys)
        tv.addLayout(th)

        self.btn_send = QPushButton("Отправить на сервер")
        self.btn_send.setStyleSheet("background-color:#FF9800;color:white;font-weight:bold;")
        self.btn_send.setEnabled(False)
        self.btn_send.clicked.connect(self.send_request)
        tv.addWidget(self.btn_send)
        tg.setLayout(tv)
        layout.addWidget(tg)

        rg = QGroupBox("Результат с сервера")
        rv = QVBoxLayout()
        self.lbl_res = QLabel("Ожидание данных...")
        self.lbl_res.setAlignment(Qt.AlignCenter)
        self.lbl_res.setMinimumHeight(60)
        self.lbl_res.setStyleSheet("""
            font-size:16px; color:#888; font-weight:bold;
            padding:10px; background-color:#1e1e1e; border-radius:5px;
        """)
        rv.addWidget(self.lbl_res)
        rg.setLayout(rv)
        layout.addWidget(rg)

        # Лог
        lg = QGroupBox("Журнал событий")
        lv = QVBoxLayout()
        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setStyleSheet("""
            QTextEdit {
                background-color:#1e1e1e; color:#d4d4d4;
                font-family:Consolas,monospace; font-size:11px;
            }
        """)
        lv.addWidget(self.txt_log)
        lg.setLayout(lv)
        layout.addWidget(lg, stretch=1)

        lh = QHBoxLayout()
        self.btn_prev = QPushButton("Лог предыдущей сессии")
        self.btn_prev.clicked.connect(self.view_previous_log)
        self.btn_save = QPushButton("Сохранить лог")
        self.btn_save.clicked.connect(self.save_log)
        lh.addWidget(self.btn_prev)
        lh.addWidget(self.btn_save)
        layout.addLayout(lh)

    def log_event(self, msg: str):
        ts = datetime.datetime.now().strftime("%H:%M:%S")
        line = f"[{ts}] {msg}"
        self.signals.log_msg.emit(line)
        with open(self.log_file, "a", encoding="utf-8") as f:
            f.write(line + "\n")

    def append_log(self, line: str):
        self.txt_log.append(line)
        self.txt_log.verticalScrollBar().setValue(self.txt_log.verticalScrollBar().maximum())

    def on_connected(self):
        self.connected = True
        self.btn_conn.setEnabled(False)
        self.btn_disconn.setEnabled(True)
        self.btn_start.setEnabled(True)
        self.lbl_sess.setText("Подключён. Ожидание начала сеанса.")
        self.lbl_sess.setStyleSheet("color:#2196F3;")
        self.log_event("Подключение к серверу установлено.")

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

    def connect_to_server(self):
        ip = self.txt_ip.text().strip()
        port = int(self.txt_port.text().strip())
        self.log_event(f"Попытка подключения к {ip}:{port}...")
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((ip, port))
            self.signals.connected.emit()
            self.recv_thread = threading.Thread(target=self.receive_loop, daemon=True)
            self.recv_thread.start()
        except Exception as e:
            QMessageBox.critical(self, "Ошибка подключения", str(e))
            self.log_event(f"Ошибка подключения: {e}")

    def receive_loop(self):
        while self.connected and self.sock:
            try:
                data = self.sock.recv(1024).decode("utf-8").strip()
                if not data:
                    break

                if data.startswith("WAIT:"):
                    self.signals.log_msg.emit("Сервер занят другим клиентом. Ожидание освобождения...")
                elif data.startswith("OK: Session started"):
                    self.signals.session_started.emit()
                elif data.startswith("OK: Session ended"):
                    self.signals.session_ended.emit()
                elif data.startswith("ERROR:"):
                    self.signals.log_msg.emit(f"Ошибка сервера: {data}")
                else:
                    self.signals.result_received.emit(data)
            except Exception as e:
                if self.connected:
                    self.signals.log_msg.emit(f"Ошибка приёма: {e}")
                break
        if self.connected:
            self.signals.disconnected.emit()

    def start_session(self):
        if not self.connected:
            return
        self.log_event("Отправка START (запрос на захват семафора)...")
        try:
            self.sock.sendall(b"START")
        except Exception as e:
            QMessageBox.critical(self, "Ошибка", str(e))

    def end_session(self):
        if not self.connected:
            return
        self.log_event("Отправка END (освобождение семафора)...")
        try:
            self.sock.sendall(b"END")
        except Exception as e:
            QMessageBox.critical(self, "Ошибка", str(e))

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
            self.lbl_res.setText("Ожидание ответа...")
            self.lbl_res.setStyleSheet("""
                font-size:16px; color:#FF9800; font-weight:bold;
                padding:10px; background-color:#1e1e1e; border-radius:5px;
            """)
        except Exception as e:
            QMessageBox.critical(self, "Ошибка отправки", str(e))

    def disconnect(self):
        if self.sock:
            try:
                self.sock.sendall(b"EXIT")
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

    def save_log(self):
        fp, _ = QFileDialog.getSaveFileName(self, "Сохранить лог", "client_log.txt", "Text files (*.txt)")
        if fp:
            try:
                with open(self.log_file, "r", encoding="utf-8") as src, open(fp, "w", encoding="utf-8") as dst:
                    dst.write(src.read())
                QMessageBox.information(self, "Успех", f"Сохранено: {fp}")
            except Exception as e:
                QMessageBox.critical(self, "Ошибка", str(e))

    def closeEvent(self, event):
        if self.connected:
            self.disconnect()
        event.accept()


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
        QLineEdit,QComboBox{background-color:#3c3c3c;color:#ddd;border:1px solid #555;padding:4px;}
    """)
    w = NumberConverterClient()
    w.show()
    sys.exit(app.exec())