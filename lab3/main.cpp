#include <iostream>
#include <unistd.h>
#include <vector>
#include <fstream>  
#include <string>

using namespace std;

void help(){
    cout<< "HELP:\n"
    << "Options:\n"
    << "  -h, --help\t\tShow this help message and exit\n"
    << "  -i, --iter\t\tCalculate Hermite polynomial using iterative method\n"
    << "  -r, --rec\t\tCalculate Hermite polynomial using recursive method\n\n"
    << "Description:\n"
    << "  This program calculates the Hermite polynomial Hn(x)\n"
    << "  using both recursive and iterative methods.\n"
    << "  Calc by formula:\n"
    << "    H0(x) = 1\n"
    << "    H1(x) = 2x\n"
    << "    Hn(x) = 2x*H(n-1)(x) - 2*(n-1)*H(n-2)(x)\n";
}

double InputDouble(const string out_msg){
    double res;
    cout << out_msg;
    while (!(cin >> res)||(cin.peek() != '\n')) {
        cin.clear();
        while (cin.get()!='\n'){};
        cout << "\nIncorrect input.\n Repeat input: ";
    }
    return res;
}

int InputInt(const string out_msg){
    int res;
    cout << out_msg;
    while (!(cin >> res)||(cin.peek() != '\n')) {
        cin.clear();
        while (cin.get()!='\n'){};
        cout << "\nIncorrect input.\n Repeat input: ";
    }
    return res;
}


double Hermit(double x, int n){
    if (n == 0) return 1;
    if (n == 1) return 2*x;
    return 2*x*Hermit(x, n-1) - 2*(n-1)*Hermit(x, n-2);
}

double HermitIter(double x, int n){//без рекурсии
    double H0 = 1, H1 = 2*x, Hn;
    if (n == 0) return H0;
    if (n == 1) return H1;
    for (int i = 2; i <= n; i++){
        Hn = 2*x*H1 - 2*(i-1)*H0;
        H0 = H1;
        H1 = Hn;
    }
    return Hn;
}

struct HermitData
{
    double x; int n;
};
//0- чтение , 1- запись
int pipe_in[2];int pipe_out[2];

void server(const char arg){
    HermitData data;
    read(pipe_in[0], &data, sizeof(HermitData));
    double res;
    if (arg == 'r'){
        cout << "Calculating using recursive method...\n";
        res = Hermit(data.x, data.n);
    }
    else if (arg == 'i'){
        cout << "Calculating using iterative method...\n";
        res = HermitIter(data.x, data.n);
    }
    write(pipe_out[1], &res, sizeof(double));
}

void client(){
    HermitData data;
    data.x = InputDouble("Input x: ");
    data.n = InputInt("Input n: ");
    write(pipe_in[1], &data, sizeof(HermitData));
    double res;
    read(pipe_out[0], &res, sizeof(double));
    cout << "H{" << data.n << "}(" << data.x << ") = " << res << endl;
}

char doArg(){
    vector<string> args;
    ifstream cmdline("/proc/self/cmdline");
    string arg;
    while (getline(cmdline, arg, '\0')) {
        args.push_back(arg);
    }
   
    for (const auto& arg : args) {
        if (arg == "--help" || arg == "-h") {
            help();
            exit(0);
        }
        if (arg == "--iter"|| arg == "-i") {
            return 'i';
        }
        if (arg == "--rec"|| arg == "-r") {
            return 'r';
        }
    }

    return 'i';
}
int main(){
    char arg = doArg();

    if (pipe(pipe_in) == -1 || pipe(pipe_out) == -1){
        cerr << "Pipe error\n";
        return 1;
    }
    pid_t pid;
    pid = fork();
    if (pid < 0){
        cerr << "Fork error\n";
        return 1;
    }

    
    if (pid == 0){
        close(pipe_in[1]);
        close(pipe_out[0]);
        server(arg);
    } else {
        close(pipe_in[0]);
        close(pipe_out[1]);
        client();
    }

    return 0;
}