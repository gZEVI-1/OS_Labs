#include <iostream> //база)
//#include <fstream>
#include <cstring> //strcmp
//#include <cstdlib>// строки в число
#include <sys/stat.h>// информация о доступе и файловая стата
#include <sys/types.h> //mode_t
#include <fcntl.h> // для open и его флагов 
#include <unistd.h> //функции unix standart
//#include <ctime> //для времени записи 
//#include <iomanip>//округление float

using namespace std;


void printHelp() {
    cout << "=== Файловый менеджер ===\n";
    cout << "Использование:\n";
    cout << "  ./filemanager [опции]\n\n";
    cout << "Опции:\n";
    cout << "  --help              Показать эту справку\n";
    cout << "  -c <src> <dst>      Копировать файл\n";
    cout << "  -m <src> <dst>      Переместить файл\n";
    cout << "  -i <file>           Информация о файле\n";
    cout << "  -p <file> <mode>    Изменить права доступа (восьмеричное, например 644)\n";
    cout << "  (без аргументов)    Интерактивный режим\n\n";
    cout << "Примеры:\n";
    cout << "  ./filemanager -c file.txt backup.txt\n";
    cout << "  ./filemanager -p script.sh 755\n";
}


void copyFile(const char* src, const char* dst) {
    
    if (strcmp(src,dst) == 0 ){
        cerr << "Ошибка: исходный и целевой файлы совпадают!\n";
        return;
    }

    const size_t BUFSIZE = 16384; 
    char buffer[BUFSIZE];
    
    
    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0) {
        perror("Ошибка открытия исходного файла");
        return;
    }

    
    struct stat st;
    if (fstat(fd_src, &st) < 0) {
        perror("Ошибка получения информации о файле");
        close(fd_src);
        return;
    }

    
    int fd_dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (fd_dst < 0) {
        perror("Ошибка создания целевого файла");
        close(fd_src);
        return;
    }

    
    ssize_t bytes_read, bytes_written;
    while ((bytes_read = read(fd_src, buffer, BUFSIZE)) > 0) {
        bytes_written = write(fd_dst, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("Ошибка записи");
            close(fd_src);
            close(fd_dst);
            return;
        }
    }

    if (bytes_read < 0) {
        perror("Ошибка чтения");
    } else {
        cout << "Файл успешно скопирован: " << src << " -> " << dst << endl;
    }

    close(fd_src);
    close(fd_dst);
}


void moveFile(const char* src, const char* dst) {
    if (rename(src, dst) == 0) {
        cout << "Файл успешно перемещён: " << src << " -> " << dst << endl;
        return;
    }

    cout << "Перемещение между разными файловыми системами, выполняется копирование...\n";
    
    copyFile(src, dst);
    
    if (unlink(src) < 0) {
        perror("Ошибка удаления исходного файла");
    } else {
        cout << "Исходный файл удалён\n";
    }
}


void fileInfo(const char* filename) {
    struct stat st;
    
    if (stat(filename, &st) < 0) {
        perror("Ошибка получения информации о файле");
        return;
    }

    cout << "\n=== Информация о файле: " << filename << " ===\n";
    
    //тип
    cout << "Тип: ";
    if (S_ISREG(st.st_mode)) cout << "обычный файл\n";
    else if (S_ISDIR(st.st_mode)) cout << "директория\n";
    else if (S_ISLNK(st.st_mode)) cout << "символическая ссылка\n";
    else if (S_ISCHR(st.st_mode)) cout << "символьное устройство\n";
    else if (S_ISBLK(st.st_mode)) cout << "блочное устройство\n";
    else if (S_ISFIFO(st.st_mode)) cout << "FIFO\n";
    else if (S_ISSOCK(st.st_mode)) cout << "сокет\n";
    else cout << "неизвестный\n";

    //размер
    cout << "Размер: " << st.st_size << " байт ";
    //права доступа
    cout << "Права доступа: ";
    cout << ((st.st_mode & S_IRUSR) ? 'r' : '-');
    cout << ((st.st_mode & S_IWUSR) ? 'w' : '-');
    cout << ((st.st_mode & S_IXUSR) ? 'x' : '-');
    cout << ((st.st_mode & S_IRGRP) ? 'r' : '-');
    cout << ((st.st_mode & S_IWGRP) ? 'w' : '-');
    cout << ((st.st_mode & S_IXGRP) ? 'x' : '-');
    cout << ((st.st_mode & S_IROTH) ? 'r' : '-');
    cout << ((st.st_mode & S_IWOTH) ? 'w' : '-');
    cout << ((st.st_mode & S_IXOTH) ? 'x' : '-');
    cout << " (" << oct << (st.st_mode & 0777) << dec << ")\n";

    //время
    // char timebuf[80];
    // struct tm *tm_info = localtime(&st.st_mtime);
    // strftime(timebuf, 80, "%Y-%m-%d %H:%M:%S", tm_info);
    // cout << "Время последнего изменения: " << timebuf << "\n";

    // tm_info = localtime(&st.st_atime);
    // strftime(timebuf, 80, "%Y-%m-%d %H:%M:%S", tm_info);
    // cout << "Время последнего доступа: " << timebuf << "\n";

    
    cout << "Количество жёстких ссылок: " << st.st_nlink << "\n";
    
    
    cout << "UID : " << st.st_uid << "\n";
    cout << "GID : " << st.st_gid << "\n\n";
}


void changePermissions(const char* filename, const char* mode_str) {
    mode_t mode = strtol(mode_str, NULL, 8);
    
    if (chmod(filename, mode) < 0) {
        perror("Ошибка изменения прав доступа");
        return;
    }
    
    cout << "Права доступа изменены: " << filename << " -> " << mode_str << endl;
}


void interactiveMode() {
    int choice;
    char filename[256], filename2[256], mode_str[8];
    
    while (true) {
        cout << "\n=== Файловый менеджер ===\n";
        cout << "1. Копировать файл\n";
        cout << "2. Переместить файл\n";
        cout << "3. Информация о файле\n";
        cout << "4. Изменить права доступа\n";
        cout << "0. Выход\n";
        cout << "Выберите действие: ";
        
        cin >> choice;
        cin.ignore(); // Очистка буфера
        
        switch (choice) {
            case 1:
                cout << "Введите имя исходного файла: ";
                cin.getline(filename, 256);
                cout << "Введите имя целевого файла: ";
                cin.getline(filename2, 256);
                copyFile(filename, filename2);
                break;
                
            case 2:
                cout << "Введите имя исходного файла: ";
                cin.getline(filename, 256);
                cout << "Введите имя целевого файла: ";
                cin.getline(filename2, 256);
                moveFile(filename, filename2);
                break;
                
            case 3:
                cout << "Введите имя файла: ";
                cin.getline(filename, 256);
                fileInfo(filename);
                break;
                
            case 4:
                cout << "Введите имя файла: ";
                cin.getline(filename, 256);
                cout << "Введите права доступа (восьмеричное, например 755): ";
                cin.getline(mode_str, 8);
                changePermissions(filename, mode_str);
                break;
                
            case 0:
                cout << "До свидания!\n";
                return;
                
            default:
                cout << "Неверный выбор!\n";
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc > 1 && (strcmp(argv[1] ,"--help") ==0) ){
        printHelp();
        return 0;
    }
    if (argc > 1) {
        char option = argv[1][1]; 
        
        switch (option) {
            case 'c': 
                if (argc < 4) {
                    cerr << "Ошибка: недостаточно аргументов для копирования\n";
                    cerr << "Использование: -c <источник> <назначение>\n";
                    return 1;
                }
                copyFile(argv[2], argv[3]);
                break;
                
            case 'm': 
                if (argc < 4) {
                    cerr << "Ошибка: недостаточно аргументов для перемещения\n";
                    cerr << "Использование: -m <источник> <назначение>\n";
                    return 1;
                }
                moveFile(argv[2], argv[3]);
                break;
                
            case 'i':
                if (argc < 3) {
                    cerr << "Ошибка: укажите имя файла\n";
                    cerr << "Использование: -i <файл>\n";
                    return 1;
                }
                fileInfo(argv[2]);
                break;
                
            case 'p':
                if (argc < 4) {
                    cerr << "Ошибка: недостаточно аргументов\n";
                    cerr << "Использование: -p <файл> <режим>\n";
                    return 1;
                }
                changePermissions(argv[2], argv[3]);
                break;
                
            default:
                cerr << "Неизвестная опция: " << argv[1] << "\n";
                cerr << "Используйте --help для справки\n";
                return 1;
        }
    } else {
        interactiveMode();
    }

    return 0;
}