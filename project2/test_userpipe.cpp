#include <iostream>
#include <stdio.h>
#include <unistd.h>

using namespace std;

bool CheckUserPipe(int recv_id, int sender_id){
    /* Exist pipe return true, else retrun -1*/
    char path[20];
    sprintf(path, "user_pipe/%d_%d", recv_id, sender_id);
    if(access(path, F_OK) != -1) {
        cout << " pipe exist" << endl;
        return true;
    }
    cout  << "pipe does not exist." << endl;
    return false;
}

int main(){
    if(CheckUserPipe(1, 0)){
        cout << "exist";
    }else{
        cout << "does not exist";
    }
}