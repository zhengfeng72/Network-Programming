#include <iostream>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h> //open
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h> //listen
#include <arpa/inet.h> //htonl
#include <vector>
#include <sys/wait.h>
#include <sstream>
#include <fcntl.h>

using namespace std;

#define N 1500
#define MAX_USER 30
#define USER_NAME 20
#define TEXT_LEN 15000
#define CMD_LEN 256
#define MSG_LEN 1024
#define ADDR_LEN 24

#define SHMKEY_USER 7890
#define SHMKEY_MSG 7891
#define SHMKEY_CLI 7892

struct User{
    bool used=false;
    bool turn=true;
    int id=0;
    pid_t pid=0;
    char name[USER_NAME];
    char address[ADDR_LEN];
    char path[20];
    int isTrigger = -1;
    int upipe[MAX_USER];
};

struct Message
{
    char msg[15000];
};

struct Cmd_Info
{
    // char intact[15000];
    // string intact;
    string name;
    vector<string> arg;
    int recv_from_id = -1;
    int send_to_id = -1;
    // bool isErrer = false;
    int after_number = 0;
    bool isNeedPipe = false;
    bool isNeedRedirect = false;
    bool isRecvUPipe = false;
    bool isSendUPipe = false;
    int Ufd_in = -1;
    int Ufd_out = -1;
    bool isUpipeError = false;
    string passFile;
    bool isErrPass;
};

/*--------------------------------------------------*/
int current_id = 0;
User* users;
int shmid_user, shmid_msg;

void Np_shell();
void Broadcast(int*, string);
void SIGHandler(int);
void Execute(Cmd_Info);
void childHandler(int);
vector<string> Split(string);
Cmd_Info Convert(vector<string> , bool);
bool CheckUserPipe(int, int);
void CloseServer(int);
/*-------------------------------------------------*/

/*
* shared memory attach detach is pair
* FIFO manual close write side, otherwise the process may hang
* SIGINT should clear shared memory
*/

int main(int argc, char* argv[]) {
    if(argc != 2) {
        cout << "Need Port Number" << endl;
        return 0;
    }

    int msock, ssock;
    unsigned short port = (unsigned short)atoi(argv[1]);
    

    if((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        cerr << "Socket create fail" << endl;
        exit(0);
    }

    const int enable =1;
    if(setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        cerr << "setsocketopt fail" << endl;
    struct sockaddr_in serv_addr, cli_addr;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if(bind(msock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        cerr << "Server: cant bind local address." << strerror(errno) << endl;
        exit(0);
    }

    listen(msock, MAX_USER);
    cout << "****************************************" << endl;
    cout << "************  Server Start  ************" << endl;
    cout << "****************************************\n" << endl;

    shmid_user = shmget(SHMKEY_USER, sizeof(User)*MAX_USER, IPC_CREAT| 0666);
    shmid_msg = shmget(SHMKEY_MSG, sizeof(Message), IPC_CREAT| 0666);
    users = (User*)shmat(shmid_user, (char*)0 ,0);

    for(int i=0; i<MAX_USER; i++){
        users[i].used = false;
        users[i].pid = 0;
        users[i].turn = true;
    }

    signal(SIGINT, CloseServer);
    signal(SIGCHLD, SIGHandler);
    while(true){
        socklen_t cli_len = sizeof(cli_addr);
        ssock = accept(msock, (sockaddr *)&cli_addr, &cli_len);

        for(current_id=0; current_id<MAX_USER; current_id++){
            if(users[current_id].used == false) break;
        }

        pid_t pid = fork();

        if(pid  == 0){
            close(msock);
            dup2(ssock, STDIN_FILENO);
            dup2(ssock, STDOUT_FILENO);
            dup2(ssock, STDERR_FILENO);
            Np_shell();
            exit(0);
        }else{

            users[current_id].used = true;
            users[current_id].pid = pid;
            users[current_id].id = current_id;
            string addr  = string(inet_ntoa(cli_addr.sin_addr)) + ":" + to_string(htons(cli_addr.sin_port));

            strcpy(users[current_id].name, "(no name)");
            strcpy(users[current_id].address, addr.c_str());

            cout << "****************************************" << endl;
            cout << "************   New Client "<< current_id <<" ***********" << endl;
            cout << "****************************************\n" << endl;
            close(ssock);
        }
    }
}

void Np_shell(){
    signal(SIGUSR1, SIGHandler);
    signal(SIGUSR2, SIGHandler);
    clearenv();

    int number_pipe[N][2];
    int ord_pipe[N][2];
    // reset
    for (size_t i = 0; i < N; i++)
    {
        for (size_t j=0; j<2; j++)
        {
            number_pipe[i][j] = -1;
            ord_pipe[i][j] = -1;
        }
    }

     /*
    * pipe_idx pipe_next_idx use ordinary pipe
    * pipe_line pipe_next_line use number pipe
    */
    int pipe_idx = 0;
    int pipe_next_idx = -1;
    int pipe_line = 0;
    setenv("PATH", "bin:.", 1);

    string welcome_msg;
    welcome_msg = string("****************************************\n")+
                        "** Welcome to the information server. **\n"+
                        "****************************************\n";
    cout << welcome_msg;
    string login_msg = string("*** User '") + users[current_id].name + "' entered from " + users[current_id].address + ". ***\n";
    Broadcast(NULL, login_msg);

    // Broadcast(&users[current_id].pid, "test broadcast\n");
    while(true){
        cout << "% ";
        string input;
        string intact;
        getline(cin, input);

        if(input=="") continue;

        if (input[input.length()-1] == '\n'){
            input = input.substr(0, input.length()-1);
            if (input[input.length()-1] == '\r'){
                input = input.substr(0, input.length()-1);
            }
        }
        intact = input;
        /* Convert string to Cmd_Info */
        vector<string> tokens = Split(input);
        vector<Cmd_Info> cmd_vector;

        vector<string> tmp_vector_str;
        for(size_t i=0; i<tokens.size(); i++){
            //tokens find '|'
            if(tokens[i].find('|') != tokens[i].npos || tokens[i].find('!') != tokens[i].npos){
                if(tokens[i].find('|') == 0 || tokens[i].find('!') == 0){
                    tmp_vector_str.push_back(tokens[i]);
                    Cmd_Info tmp_cmd = Convert(tmp_vector_str, true);
                    // strcpy(tmp_cmd.intact, input.c_str());
                    // tmp_cmd.intact = input;
                    tmp_vector_str.clear();
                    cmd_vector.push_back(tmp_cmd);
                }else{
                    tmp_vector_str.push_back(tokens[i]);
                }
            }else{
                tmp_vector_str.push_back(tokens[i]);
            }
        }
        // don't include '|' '!'
        if(!tmp_vector_str.empty()){
            Cmd_Info tmp_cmd = Convert(tmp_vector_str, false);
            tmp_cmd.after_number = 1;
            // tmp_cmd.intact = input;
            // strcpy(tmp_cmd.intact, input.c_str());
            cmd_vector.push_back(tmp_cmd);
        }
        /* End to Convert*/

        bool isSingleCommand = cmd_vector.size()==1 ?true :false;
        bool isLastCommand = false;
        for(size_t i=0; i<cmd_vector.size(); i++){
            if(i == cmd_vector.size()-1) isLastCommand = true;
            /* Set Pipe*/
            int pipe_next_line = 0;
            if(cmd_vector[i].after_number == 0){ /* use ordinary pipe */
                pipe_next_idx = (pipe_idx + 1) % N;
            }else{ /* use number pipe */
                pipe_next_idx = 0;
                pipe_next_line = (pipe_line + cmd_vector[i].after_number) % N;
            }

            /* need pipe to use(write) */
            if (cmd_vector[i].isNeedPipe || cmd_vector[i].isErrPass){
                if(cmd_vector[i].after_number == 0){ /* use ordinary pipe */
                    if(ord_pipe[pipe_next_idx][0] < 0){
                        pipe(ord_pipe[pipe_next_idx]);
                    }
                }else{ /* use number pipe */
                    if(number_pipe[pipe_next_line][0] < 0){
                        pipe(number_pipe[pipe_next_line]);
                    }
                }
            }
            /* End Setting Pipe*/

            /* Set User Pipe*/
            /* <N(receive) first, then >N(send) */
            string message;
            if(cmd_vector[i].recv_from_id != -1){
                if(!users[cmd_vector[i].recv_from_id].used){ /* dosesnt exist */
                    cout << "*** Error: user #" << cmd_vector[i].recv_from_id+1 << " does not exist yet. ***" << endl;
                    continue;
                }else{
                    if(CheckUserPipe(current_id, cmd_vector[i].recv_from_id)){ /* exist pipe */
                        User receiver = users[current_id];
                        User sender = users[cmd_vector[i].recv_from_id];
                        message = string("*** ") + receiver.name + " (#" + to_string(receiver.id+1) + ") just received from " + sender.name + " (#" + to_string(sender.id+1)+ ") by '" + intact + "' ***\n";
                        Broadcast(NULL, message);

                        cmd_vector[i].Ufd_in = users[current_id].upipe[cmd_vector[i].recv_from_id];
                    }else{
                        cout << "*** Error: the pipe #" << cmd_vector[i].recv_from_id+1 << "->#" << current_id+1 << " does not exist yet. ***" << endl;
                        continue;
                    }
                }
            }

            /* make sure two signal will not to close*/
            if(cmd_vector[i].recv_from_id != -1 && cmd_vector[i].send_to_id != -1) usleep(5000);

            if(cmd_vector[i].send_to_id != -1){
                if(!users[cmd_vector[i].send_to_id].used){
                    cout << "*** Error: user #" << cmd_vector[i].send_to_id+1 << " does not exist yet. ***" << endl;
                    continue;
                }else{
                    if(!CheckUserPipe(cmd_vector[i].send_to_id, current_id)){ /* doesnt exit pipe*/
                        User receiver = users[cmd_vector[i].send_to_id];
                        User sender = users[current_id];
                        message = string("*** ")+sender.name+" (#"+to_string(sender.id+1)+") just piped '" + intact+"' to "+receiver.name+" (#"+to_string(receiver.id+1)+") ***\n";
                        Broadcast(NULL,message);

                        char path[20];
                        sprintf(path, "user_pipe/%d_%d", receiver.id, sender.id);
                        mkfifo(path, S_IFIFO|0666);
                        strcpy(users[cmd_vector[i].send_to_id].path, path);
                        kill(receiver.pid, SIGUSR2);
                        users[cmd_vector[i].send_to_id].isTrigger = current_id;
                        cmd_vector[i].Ufd_out = open(path, O_WRONLY);
                    }else{
                        cout << "*** Error: the pipe #" << current_id+1 << "->#" << cmd_vector[i].send_to_id+1 << " already exists. ***" << endl;
                        continue;
                    }
                }
            }
            /* End Setting Pipe*/

            /* System Command */
            if(cmd_vector[i].name == "exit"){
                message = string("*** User '") + users[current_id].name + "' left. ***\n";
                Broadcast(NULL, message);
                /*clear user data*/
                users[current_id].used = false;
                char s[20];
                for(int j=0; j<MAX_USER; j++){
                    if(CheckUserPipe(j, current_id)){
                        sprintf(s,"user_pipe/%d_%d", j, current_id);
                        remove(s);
                    }
                    if(CheckUserPipe(current_id, j)){
                        sprintf(s,"user_pipe/%d_%d", current_id, j);
                        remove(s);
                    }
                }
                shmdt(users);
                exit(0);
            }else if(cmd_vector[i].name == "who"){
                message = string("<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
                for(size_t i=0; i<MAX_USER; i++){
                    if(users[i].used){
                        message += to_string(i+1) + "\t" + users[i].name + "\t" + users[i].address;
                        message += i == current_id ?"\t<-me\n" : "\n";
                    }
                }
                Broadcast(&(users[current_id].pid), message);
                continue;
            }else if(cmd_vector[i].name == "tell"){
                /*
                * arg[0] receive_id string
                * arg[1] send_message string
                */
                int receive_id = stoi(cmd_vector[i].arg[0])-1;
                string tell_msg;
                for(size_t j=1;j<cmd_vector[i].arg.size(); j++){
                    tell_msg += cmd_vector[i].arg[j];
                    if(j!=cmd_vector[i].arg.size()-1)tell_msg +=" ";
                }
                if(users[receive_id].used){
                    message = string("*** ") + users[current_id].name +" told you ***: " + tell_msg + "\n";
                    Broadcast(&(users[receive_id].pid), message);
                }else{
                    message = string("*** Error: user #") + to_string(receive_id+1) + " does not exist yet. ***\n";
                    Broadcast(&(users[current_id].pid), message);
                }
                continue;
            }else if(cmd_vector[i].name == "yell"){
                string yell_msg;
                for(size_t j=0; j<cmd_vector[i].arg.size(); j++){
                    yell_msg += cmd_vector[i].arg[j];
                    if(j!=cmd_vector[i].arg.size()-1)
                        yell_msg += " ";
                }
                message = string("*** ") + users[current_id].name + " yelled ***: " + yell_msg + "\n";
                Broadcast(NULL, message);
                continue;
            }else if(cmd_vector[i].name == "name"){
                string new_name = cmd_vector[i].arg[0];
                bool isNameExist = false;
                for(size_t j=0; j<MAX_USER; j++){
                    if(new_name == users[j].name){
                        message = string("*** User '") + new_name + "' already exists. ***\n";
                        cout << message;
                        isNameExist = true;
                        break;
                    }
                }
                if(isNameExist)continue;
                strcpy(users[current_id].name, new_name.c_str());
                message = string("*** User from ") + users[current_id].address + " is named '" + users[current_id].name + "'. ***\n";
                Broadcast(NULL, message);
                continue;
            }else if(cmd_vector[i].name == "setenv"){
                setenv(cmd_vector[i].arg[0].c_str(), cmd_vector[i].arg[1].c_str(), 1);
                continue;
            }else if(cmd_vector[i].name == "printenv"){
                if(getenv(cmd_vector[i].arg[0].c_str()) != NULL){
                    string env = getenv(cmd_vector[i].arg[0].c_str());
                    cout << env << endl;
                }
                continue;
            }

            /* Command */
            pid_t pid = fork();
            signal(SIGCHLD, childHandler);
            if(pid > 0){
                /* child take pipe then close */
                if(ord_pipe[pipe_idx][0] >= 0){
                    close(ord_pipe[pipe_idx][0]);
                    close(ord_pipe[pipe_idx][1]);
                    ord_pipe[pipe_idx][0] = -1;
                    ord_pipe[pipe_idx][1] = -1;
                }

                // if after numbre > 1 maybe write sth into pipe, so cant close pipe write
                // afternumber :0/1 
                if(number_pipe[pipe_next_line][1] >= 0 && cmd_vector[i].after_number <= 1){
                    close(number_pipe[pipe_next_line][1]);
                    number_pipe[pipe_next_line][1] = -1;
                    // cout << "Parent close number pipe  [" << pipe_next_line << "] w" << endl;
                }

                usleep(150000);
                if (isSingleCommand || cmd_vector[i].isNeedRedirect){
                    int status;
                    waitpid(pid, &status, 0); // WNOWAIT : leave the child in a waitable state; a later
                                            //              wait call can be used again retrieve the child status info.
                }else{
                    int status;
                    waitpid(-1, &status, WNOHANG);
                }

                if(cmd_vector[i].isSendUPipe ){
                    close(cmd_vector[i].Ufd_out);
                }

                if(cmd_vector[i].isRecvUPipe){
                    char path[20];
                    User receiver = users[current_id];
                    User sender = users[cmd_vector[i].recv_from_id];
                    sprintf(path, "user_pipe/%d_%d", receiver.id, sender.id);
                    remove(path);
                }
            }else if(pid == 0){
                if (cmd_vector[i].isNeedPipe || cmd_vector[i].isErrPass){
                    if(cmd_vector[i].after_number == 0){ /* use ordinary pipe */
                        dup2(ord_pipe[pipe_next_idx][1], STDOUT_FILENO);
                        if(cmd_vector[i].isErrPass) dup2(ord_pipe[pipe_next_idx][1], STDERR_FILENO);
                        close(ord_pipe[pipe_next_idx][0]);
                        close(ord_pipe[pipe_next_idx][1]);
                        // cout << "Cild dup ordinary pipe " << pipe_next_idx << " to cout" << endl;
                    }else{ /* use number pipe*/
                        dup2(number_pipe[pipe_next_line][1], STDOUT_FILENO);
                        if(cmd_vector[i].isErrPass) dup2(number_pipe[pipe_next_line][1], STDERR_FILENO);
                        close(number_pipe[pipe_next_line][0]);
                        close(number_pipe[pipe_next_line][1]);
                        // cout << "Cild dup number pipe " << pipe_next_line << " to cout" << endl;
                    }
                }

                /* check previous number pipe*/
                if(number_pipe[pipe_line][0] >= 0){
                    dup2(number_pipe[pipe_line][0], STDIN_FILENO);
                    close(number_pipe[pipe_line][0]);
                    close(number_pipe[pipe_line][1]);
                    // cout << "Cild dup number pipe" << pipe_line << " to cin" << endl;
                }

                /* check previous ordinary pipe*/
                if(ord_pipe[pipe_idx][0] >= 0){
                    dup2(ord_pipe[pipe_idx][0], STDIN_FILENO);
                    close(ord_pipe[pipe_idx][0]);
                    close(ord_pipe[pipe_idx][1]);
                    // cout << "Cild dup ordinary pipe " << pipe_idx << " to cin" << endl;
                }

                if(cmd_vector[i].isNeedRedirect){
                    ord_pipe[pipe_next_idx][1] = open(cmd_vector[i].passFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
                    dup2(ord_pipe[pipe_next_idx][1], 1);
                }

                if(cmd_vector[i].isRecvUPipe){
                    dup2(cmd_vector[i].Ufd_in, STDIN_FILENO);
                }

                if(cmd_vector[i].isSendUPipe){
                    dup2(cmd_vector[i].Ufd_out, STDOUT_FILENO);
                    dup2(cmd_vector[i].Ufd_out, STDERR_FILENO);
                    // close(cmd_vector[i].Ufd_out);
                }

                Execute(cmd_vector[i]);
            }else{
                perror("fork error");
                exit(-1);
            }
            /* renew ordinary pipe index */
            pipe_idx = cmd_vector[i].after_number == 0 ? (pipe_idx + 1) % N : 0;
            // cout << "Renew pipe_idx " <<  users[current_id].pipe_idx << endl;
            /* 
            * renew number pipe line
            * the last command of input or the command which has |number, then pipe_line should jump to next
            */
            if ( isLastCommand || cmd_vector[i].after_number != 0) pipe_line = (pipe_line + 1) % N;
            // cout << "Renew pipe_line " << users[current_id].pipe_line << endl;
        }
    }
}

bool CheckUserPipe(int recv_id, int sender_id){
    /* Exist pipe return true, else retrun -1*/
    char path[20];
    sprintf(path, "user_pipe/%d_%d", recv_id, sender_id);
    if(access(path, F_OK) != -1) {
        return true;
    }
    return false;
}

void SIGHandler(int signo){
    if(signo == SIGUSR1){
        Message* broadcast_msg = (Message*) shmat(shmid_msg, (char*)0, 0);
        cout << broadcast_msg->msg;
        shmdt(broadcast_msg);
    }else if(signo== SIGUSR2){
        if(users[current_id].isTrigger!= -1){
            int sender_id = users[current_id].isTrigger;
            users[current_id].upipe[sender_id] = open(users[current_id].path, O_RDONLY);
        }
    }else if(signo == SIGCHLD){
        int status;
        while(waitpid(-1, &status, WNOHANG)>0){}
    }
}

void CloseServer(int singo){
    char s[20];
    for(int j=0; j<MAX_USER; j++){
        for(int k=0; k<MAX_USER; k++){
            if(CheckUserPipe(j, k)){
                sprintf(s,"user_pipe/%d_%d", j, k);
                remove(s);
            }
        }
    }
    shmdt(users);
    shmctl(shmid_user, IPC_RMID, (shmid_ds*)0);
    shmctl(shmid_msg, IPC_RMID, (shmid_ds*)0);
    exit(1);
}

void Broadcast(int* target, string message){
    for(int j=0; j<message.size()-1; j++){
        if( message[j] == '\r'){
            message = message.replace(message.begin()+j, message.begin()+j+1, "");
            j--;
        }
    }
    const char *m = message.c_str();
    Message* broadcast_msg = (Message*) shmat(shmid_msg, (char*)0, 0);
    strcpy(broadcast_msg->msg, m);

    if(target == NULL) { /*Broadcast*/
        for(size_t i=0; i<MAX_USER; i++){
            if(users[i].used){
                kill(users[i].pid, SIGUSR1);
            }
        }
    }else {
        kill(*target, SIGUSR1);
    }
    shmdt(broadcast_msg);
}

void Execute(Cmd_Info cmd){
    // besides args, execvp args need command name
    // command name put head, NULL put tail
    char *cus_argv[cmd.arg.size() + 2];

    // put command name into args[0]
    cus_argv[0] = new char(cmd.name.size() + 1);
    strcpy(cus_argv[0], cmd.name.c_str());

    // put other args into args[1~ end-1]
    if(!cmd.arg.empty()){
        for (size_t i = 1; i <= cmd.arg.size(); i++){
            cus_argv[i] = new char(cmd.arg[i - 1].size() + 1);
            strcpy(cus_argv[i], cmd.arg[i - 1].c_str());
        }
    }
    // args[end] = NULL
    cus_argv[cmd.arg.size() + 1] = NULL;

    execvp(cmd.name.c_str(), cus_argv);

    cerr << "Unknown command: [" << cmd.name << "]." << endl;
    // fprintf(stdout, "Unknown command: [%s].", cmd_vector[i].name.c_str());
    exit(0);
}

vector<string> Split(string input){
    vector<string> result;
    result.clear();
    istringstream ss(input);
    string s;
    while(ss>>s){
        result.push_back(s);
    }
    return result;
}

Cmd_Info Convert(vector<string> tmp_vector_string, bool isInclude){
    /*
    *vector string:
    *    begin = cmd.name
    *    middle elements = cmd.arg
    *    end = check after_number isNeedPipe isErrPass
    */
    Cmd_Info tmp_cmd;
    tmp_cmd.name = tmp_vector_string[0];
    int num = tmp_vector_string.size();
    // include | !
    if(isInclude){
        string last = tmp_vector_string[num - 1];
        char type = last[0];

        string number = last.erase(0, 1);
        if(!number.empty()){
            tmp_cmd.after_number = stoi(number);
        }

        tmp_vector_string.erase(tmp_vector_string.begin());
        tmp_vector_string.erase(tmp_vector_string.end());

        if(type == '|')
            tmp_cmd.isNeedPipe = true;
        else if(type == '!'){
            tmp_cmd.isErrPass = true;
            tmp_cmd.isNeedPipe = true;
        }
    }else{
        tmp_vector_string.erase(tmp_vector_string.begin());
        tmp_cmd.isNeedPipe = false;
        tmp_cmd.isErrPass = false;
    }

    // set redirect and user pipe
    if(tmp_cmd.name == "yell" || tmp_cmd.name == "tell" || tmp_cmd.name == "name"){
        tmp_cmd.arg = tmp_vector_string;
        return tmp_cmd;
    }

    for(size_t i = 0; i < tmp_vector_string.size(); i++){
        if (tmp_vector_string[i] == ">"){
            tmp_cmd.isNeedRedirect = true;
            tmp_cmd.passFile = tmp_vector_string[i + 1];
            tmp_vector_string.erase(tmp_vector_string.begin() + i + 1); /* erase filename*/
            tmp_vector_string.erase(tmp_vector_string.begin() + i); /* erase > */
            break;
        }else if(tmp_vector_string[i].find(">") != tmp_vector_string[i].npos){
            string tmp = tmp_vector_string[i];
            int number = stoi(tmp.erase(0,1))-1;
            tmp_cmd.send_to_id = number;
            tmp_cmd.isSendUPipe = true;
            tmp_vector_string.erase(tmp_vector_string.begin() + i); /* erase >number */
            i--;
        }else if(tmp_vector_string[i].find("<") != tmp_vector_string[i].npos){
            string tmp = tmp_vector_string[i];
            int number = stoi(tmp.erase(0,1))-1;
            tmp_cmd.recv_from_id = number;
            tmp_cmd.isRecvUPipe = true;
            tmp_vector_string.erase(tmp_vector_string.begin() + i); /* erase <number */
            i--;
        }
    }
    tmp_cmd.arg = tmp_vector_string;
    return tmp_cmd;
}

void childHandler(int signo){
    /*
    let the parant process know the child process has terminated
    */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        ;
}