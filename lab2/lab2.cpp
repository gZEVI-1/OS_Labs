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
    cout << "Примеры:\n";
    cout << "  ./filemanager -c file.txt backup.txt\n";
    cout << "  ./filemanager -p script.sh 755\n";
}


void copyFile(const char* src, const char* dst) {
    
    if (strcmp(src,dst) == 0 ){
        cerr << "Ошибка: исходный и целевой файлы совпадают!\n";
        return;
    }

    const int BUFSIZE= 1024;
    char buffer[BUFSIZE];
    
    
    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0) {
        cerr <<"Ошибка открытия исходного файла";
        return;
    }

    
    struct stat st;
    int fd_dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);
    if (fd_dst < 0) {
        cerr <<"Ошибка создания целевого файла";
        close(fd_src);
        return;
    }

    
    ssize_t bytes_read, bytes_written;
    while ((bytes_read = read(fd_src, buffer, BUFSIZE)) > 0) {
        bytes_written = write(fd_dst, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            cerr <<"Ошибка записи";
            close(fd_src);
            close(fd_dst);
            return;
        }
    }

    if (bytes_read < 0) {
        cerr <<"Ошибка чтения";
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
        cerr <<"Ошибка удаления исходного файла";
    } else {
        cout << "Исходный файл удалён\n";
    }
}


void fileInfo(const char* filename) {
    struct stat st;
    
    if (stat(filename, &st) < 0) {
        cerr <<"Ошибка получения информации о файле";
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

    

    
    cout << "Количество жёстких ссылок: " << st.st_nlink << "\n";
    
    
    cout << "UID : " << st.st_uid << "\n";
    cout << "GID : " << st.st_gid << "\n\n";
}


void changePermissions(const char* filename, const char* mode_str) {
    mode_t mode = strtol(mode_str, NULL, 8);
    
    if (chmod(filename, mode) < 0) {
        cerr <<"Ошибка изменения прав доступа";
        return;
    }
    
    cout << "Права доступа изменены: " << filename << " -> " << mode_str << endl;
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
            cout << "Rоманда не введена\n";
    }

    return 0;
}