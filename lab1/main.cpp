#include <iostream>
#include <vector>
using namespace std;

double input(int index){
    double num;
    cout<< "input "<<index<<" elem:\n";
    while(!(cin>>num)|| cin.peek()!='\n'){
        cin.clear();
        while (cin.get()!='\n'){
            cout<<"Incorrect input. Try again\n";
        }
    }   
    return num; 
}

int input(){
    int num;
    cout<< "Input len of array \n";
    while(!(cin>>num)|| cin.peek()!='\n'){
        cin.clear();
        while (cin.get()!='\n'){
            cout<<"Incorrect input. Try again\n";
        }   
    }
    return num;
}

double sumArray(vector<double>& arr, int size) {
    double sum = 0;
    for (int i = 0; i < size; ++i) {
        sum += arr[i];
    }
    return sum;
}
 
int main(){
    int size=input();
    vector<double> arr(size);

    for(int i=0;i<size;i++){
        arr[i]=input(i);
    }
    cout<<"Summ of array: "<< sumArray(arr, size)<<'\n'; 
    return 0;
}
