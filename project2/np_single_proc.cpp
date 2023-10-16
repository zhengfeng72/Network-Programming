#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <string>
#include <cstring>
#include <string.h>
#include <vector>
#include <unistd.h> //pipe dup2
#include <fcntl.h> //open
#include <arpa/inet.h> //inet_ntoa
#include <sstream> //Split
#include <map>
#include <stdlib.h> //env

using namespace std;

#define N 1500
#define MAX_USER 30
#define TEXTLEN 15000


struct UserPipe
{
    int recv_id;
    int send_id;
    int fd[2];
};

vector<UserPipe> userpipe;

struct User {
    bool used;
    bool turn;
    int ssock;
    int id=0;
    string name;
    string address;
    map<string, string> envMap;
    int number_pipe[N][2];
    int ord_pipe[N][2];
    int pipe_idx=0;
    int pipe_next_idx=0;
    int pipe_line=0;

    void Init(){
        used = false;
        turn = true;
        ssock = 0;
        name = "(no name)";
        address = "";

        envMap.clear();
        envMap = {{"PATH", "bin:."}};

        /* init pipe */
        pipe_idx = 0;
        pipe_next_idx = 0;
        pipe_line = 0;
        for(size_t i = 0; i < N; i++){
            for(size_t j=0; j<2; j++){
                number_pipe[i][j] = -1;
                ord_pipe[i][j] = -1;
                close(number_pipe[i][j]);
                close(ord_pipe[i][j]);
            }
        }
        /* clear user pipe */
        for(size_t i=0; i<userpipe.size(); i++){
            if( userpipe[i].send_id==id || userpipe[i].recv_id==id){
                close(userpipe[i].fd[0]);
                close(userpipe[i].fd[1]);
                userpipe.erase(userpipe.begin()+i);
            }
        }
        id = 0;
    }
};

struct Cmd_Info
{
    string intact;
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
    int Ufd_in = 0;
    int Ufd_out = 0;
    bool isUpipeError = false;
    string passFile;
    bool isErrPass;
    int pipeToCmd = 0; /* pipe to command line, number pipe, same as pipe_next_line*/
};


/* --------------------- */
fd_set activefds, readfds;
User users[MAX_USER];
int current_id;
int FD_NULL = open("/dev/null", O_RDWR);

int CreateSocket(unsigned int);
void ClientConnet(int);
void ClientHandle(int);
void Broadcast(int*, string);
vector<string> Split(string);
Cmd_Info Convert(vector<string>, bool);
void SetPipe(Cmd_Info*);
void SetUserPipe(Cmd_Info*);
void HandleCommand(Cmd_Info, bool, bool);
void Execute(Cmd_Info);
void Print(vector<Cmd_Info>);
void childHandler(int);
int CheckUserPipe(int,int);
/* --------------------- */

void childHandler(int signo){
    /*
    let the parant process know the child process has terminated
    */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0){};
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

void Broadcast(int* target, string message) {
    const char *m = message.c_str();
    
    if(target == NULL) { /*Broadcast*/
        for(size_t i=0; i<MAX_USER; i++){
            if(users[i].used){
                write(users[i].ssock,m,message.length());
            }
        }
    }else {
        write(*target, m, message.length());
    }
}

void Print(vector<Cmd_Info> cmd){
    for (size_t i = 0; i < cmd.size(); i++){
        cout << "command " << i << endl;
        cout << "name : [" << cmd[i].name << "]" << endl;
        cout << "arg : ";
        for (size_t j = 0; j < cmd[i].arg.size(); j++)
        {
            cout << "[" << cmd[i].arg[j] << "]";
        }
        cout << endl;
        cout << "send_id : " << cmd[i].send_to_id << endl;
        cout << "recv id : " << cmd[i].recv_from_id << endl;
        // cout << "after number : " << cmd[i].after_number << endl;
        // cout << "isNeedPipe : " << cmd[i].isNeedPipe << endl;
        // cout << "isNeedRedirect : " << cmd[i].isNeedRedirect << endl;
        // cout << "passFile : " << cmd[i].passFile << endl;
        // cout << "isErrPass : " << cmd[i].isErrPass << endl;
        cout << "-------------------" << endl;
    }
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

int CreateSocket(unsigned int port) {
    int msock;
    struct sockaddr_in serv_addr;

    if((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        cerr << "Socket create fail" << endl;
        exit(0);
    }

    const int enable =1;
    if(setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        cerr << "setsocketopt fail" << endl;
    /*
    * bind our local address so that the client can send to us.
    */
   bzero((char *)&serv_addr, sizeof(serv_addr));
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
    cout << "****************************************" << endl;

    return msock;
}

void ClientConnet(int msock) {
    sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    int ssock;

    for(size_t i=0; i<MAX_USER; i++){
        if(users[i].used == true) continue;
        else{
            string welcome_msg;
            welcome_msg = string("****************************************\n")+
                        "** Welcome to the information server. **\n"+
                        "****************************************\n";
            if((ssock = accept(msock, (sockaddr *)&cli_addr, &clilen)) ){
                users[i].used = true;
                users[i].ssock = ssock;
                users[i].id = i;
                users[i].address = string(inet_ntoa(cli_addr.sin_addr)) + ":" + to_string(htons(cli_addr.sin_port));
            
                FD_SET(ssock, &activefds);
                Broadcast(&ssock, welcome_msg);

                string login_msg = string("*** User '") + users[i].name + "' entered from " + users[i].address + ". ***\n";
                Broadcast(NULL, login_msg);
            }
            break;
        }
    }
}

void ClientHandle(int id){
    if(!users[id].used)return;

    /* Set environment variable*/
    clearenv();
    for(auto i : users[current_id].envMap){
        setenv(i.first.c_str(), i.second.c_str(), 1);
    }

    /* Receive Client Message*/
    char text[TEXTLEN];
    string input;
    if(users[id].turn){
        Broadcast(&(users[id].ssock), "% ");
        users[id].turn = false;
    }

    if(FD_ISSET(users[id].ssock, &readfds) > 0){
        bzero(text, sizeof(text));
        int n = read(users[id].ssock, text, sizeof(text));
        if (n > 0){
            input = text;
            if (input[input.length()-1] == '\n'){
                input = input.substr(0, input.length()-1);
                if (input[input.length()-1] == '\r'){
                    input = input.substr(0, input.length()-1);
                }
            }
            cout << "Client " << current_id << " : [" << input << "]" << endl;
        }
    }else {
        return;
    }

    if(input.empty() || input.find_first_not_of(" ", 0) == string::npos){
        users[id].turn = true;
        return;
    }

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
                tmp_cmd.intact = input;
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
        tmp_cmd.intact = input;
        cmd_vector.push_back(tmp_cmd);
    }
    /* End to Convert*/

    //Print(cmd_vector);

    bool isSingleCommand = cmd_vector.size()==1 ?true :false;
    bool isLastCommand = false;
    for(size_t i=0; i<cmd_vector.size(); i++){
        if(i == cmd_vector.size()-1) isLastCommand = true;
        SetPipe(&cmd_vector[i]);
        SetUserPipe(&cmd_vector[i]);
        HandleCommand(cmd_vector[i], isSingleCommand, isLastCommand);
        //cout << "Done [" << cmd_vector[i].name << "]" << endl;
    }

    users[id].turn = true;
    return;
}

void SetPipe(Cmd_Info* cmd){
    // set pipe
    // cout << "Execute Command " << cmd->name << endl;
    int pipe_next_line = 0;
    if(cmd->after_number == 0){ /* use ordinary pipe */
        users[current_id].pipe_next_idx = (users[current_id].pipe_idx + 1) % N;
    }else{ /* use number pipe */
        users[current_id].pipe_next_idx = 0;
        pipe_next_line = (users[current_id].pipe_line + cmd->after_number) % N;
    }
    /* Test */
    // cout << "idx :" << users[current_id].pipe_idx << ", next_idx :" << users[current_id].pipe_next_idx << endl;
    // cout << "line :" << users[current_id].pipe_line << ",next_line :" << pipe_next_line << endl;

    /* need pipe to use(write) */
    int pipe_next_idx = users[current_id].pipe_next_idx;
    if (cmd->isNeedPipe || cmd->isErrPass){
        if(cmd->after_number == 0){ /* use ordinary pipe */
            if(users[current_id].ord_pipe[pipe_next_idx][0] < 0){
                pipe(users[current_id].ord_pipe[pipe_next_idx]);
                // cout << "create ordinary pipe :" <<  pipe_next_idx << endl;
            }
        }else{ /* use number pipe */
            if(users[current_id].number_pipe[pipe_next_line][0] < 0){
                pipe(users[current_id].number_pipe[pipe_next_line]);
                // cout << "create number pipe :" << pipe_next_line << endl;
            }
        }
    }

    cmd->pipeToCmd = pipe_next_line;
}

int CheckUserPipe(int recv_id, int sender_id){
    /* Exist pipe return pipe index, else retrun -1*/
    int index = -1;
    for(int i=0; i<userpipe.size(); i++){
        // cout << "pipe receiver id" << userpipe[i].recv_id << ", sender id" << userpipe[i].send_id << endl;
        if(userpipe[i].recv_id == recv_id && userpipe[i].send_id == sender_id){
            index = i;
            break;
        }
    }
    // cout << "index " << index << endl;
    return index;
}

void SetUserPipe(Cmd_Info* cmd){
    /* <N(receive) first, then >N(send) */
    string message;
    if(cmd->recv_from_id != -1){
        if(!users[cmd->recv_from_id].used){ /* dosesnt exist */
            message = string("*** Error: user #") + to_string(cmd->recv_from_id+1) + " does not exist yet. ***\n";
            Broadcast(&(users[current_id].ssock), message);
            cmd->Ufd_in = FD_NULL;
            cmd->isUpipeError = true;
        }else{
            int index;
            if((index = CheckUserPipe(current_id, cmd->recv_from_id)) > -1){ /* exist pipe*/
                User receiver = users[current_id];
                User sender = users[cmd->recv_from_id];
                message = string("*** ") + receiver.name + " (#" + to_string(receiver.id+1) + ") just received from " + sender.name + " (#" + to_string(sender.id+1)+ ") by '" + cmd->intact + "' ***\n";
                Broadcast(NULL, message);

                /* receive from user pipe*/
                cmd->Ufd_in = userpipe[index].fd[0];
                cout << "receive from user pipe, receive id " << userpipe[index].recv_id+1 << ", sender id" << userpipe[index].send_id+1<<endl;
            }else{
                message = string("*** Error: the pipe #")+to_string(cmd->recv_from_id+1)+"->#"+to_string(current_id+1)+" does not exist yet. ***\n";
                Broadcast(&(users[current_id].ssock), message);
                cmd->Ufd_in = FD_NULL;
                cmd->isUpipeError = true;
            }
        }
    }
    if(cmd->send_to_id != -1){
        if(!users[cmd->send_to_id].used){
            message = string("*** Error: user #") + to_string(cmd->send_to_id+1) + " does not exist yet. ***\n";
            Broadcast(&(users[current_id].ssock), message);
            cmd->Ufd_out = FD_NULL;
            cmd->isUpipeError = true;
        }else{
            int index;
            if((index = CheckUserPipe(cmd->send_to_id, current_id)) == -1 ){ /* doesnt exit pipe*/
                User receiver = users[cmd->send_to_id];
                User sender = users[current_id];
                message = string("*** ")+sender.name+" (#"+to_string(sender.id+1)+") just piped '" + cmd->intact+"' to "+receiver.name+" (#"+to_string(receiver.id+1)+") ***\n";
                Broadcast(NULL,message);

                UserPipe temp_pipe;
                temp_pipe.send_id = current_id;
                temp_pipe.recv_id = cmd->send_to_id;
                pipe(temp_pipe.fd);
                userpipe.push_back(temp_pipe);
                cmd->Ufd_out = temp_pipe.fd[1];
                cout << "create pipe receiver id "<<temp_pipe.recv_id+1 << " sender id " << temp_pipe.send_id+1 <<endl;
            }else{
                message = string("*** Error: the pipe #")+to_string(current_id+1)+"->#"+to_string(cmd->send_to_id+1)+" already exists. ***\n";
                Broadcast(&(users[current_id].ssock), message);
                cmd->Ufd_out = FD_NULL;
                cmd->isUpipeError = true;
            }
        }
    }
}

void HandleCommand(Cmd_Info cmd, bool isSingleCommand, bool isLastCommand){
    string message;
    /* System Command */
    if(cmd.name == "exit"){
        message = string("*** User '") + users[current_id].name + "' left. ***\n";
        Broadcast(NULL, message);
        /*clear user data*/
        FD_CLR(users[current_id].ssock, &activefds);
        close(users[current_id].ssock);
        users[current_id].Init();
        int status;
        while(waitpid(-1, &status, WNOHANG)>0){};
        return;
    }else if(cmd.name == "who"){
        message = string("<ID>\t<nickname>\t<IP:port>\t<indicate me>\n");
        for(size_t i=0; i<MAX_USER; i++){
            if(users[i].used){
                message += to_string(i+1) + "\t" + users[i].name + "\t" + users[i].address;
                message += i == current_id ?"\t<-me\n" : "\n";
            }
            
        }
        Broadcast(&(users[current_id].ssock), message);
        return;
    }else if(cmd.name == "tell"){
        /*
        * arg[0] receive_id string
        * arg[1] send_message string
        */
        
        int receive_id = stoi(cmd.arg[0])-1;
        string tell_msg;
        for(size_t i=1;i<cmd.arg.size(); i++){
            tell_msg += cmd.arg[i];
            if(i!=cmd.arg.size()-1)tell_msg +=" ";
        }
        if(users[receive_id].used){
            message = string("*** ") + users[current_id].name +" told you ***: " + tell_msg + "\n";
            Broadcast(&(users[receive_id].ssock), message);
        }else{
            message = string("*** Error: user #") + to_string(receive_id+1) + " does not exist yet. ***\n";
            Broadcast(&(users[current_id].ssock), message);
        }
        return;
    }else if(cmd.name == "yell"){
        string yell_msg;
        for(size_t i=0; i<cmd.arg.size(); i++){
            yell_msg += cmd.arg[i];
            if(i!=cmd.arg.size()-1)
                yell_msg += " ";
        }
        message = string("*** ") + users[current_id].name + " yelled ***: " + yell_msg + "\n";
        Broadcast(NULL, message);
        return;
    }else if(cmd.name == "name"){
        string new_name = cmd.arg[0];
        for(size_t i=0; i<MAX_USER; i++){
            if(new_name == users[i].name){
                message = string("*** User '") + new_name + "' already exists. ***\n";
                Broadcast(&(users[current_id].ssock), message);
                return;
            }
        }
        users[current_id].name = new_name;
        message = string("*** User from ") + users[current_id].address + " is named '" + users[current_id].name + "'. ***\n";
        Broadcast(NULL, message);
        return;
    }else if(cmd.name == "setenv"){
        //setenv(cmd.arg[0].c_str(), cmd.arg[1].c_str(), 1);
        users[current_id].envMap[cmd.arg[0]] = cmd.arg[1];
        return;
    }else if(cmd.name == "printenv"){
        if(getenv(cmd.arg[0].c_str()) != NULL){
            string env = getenv(cmd.arg[0].c_str());
            Broadcast(&(users[current_id].ssock), env+"\n");
        }
        return;
    }

    /* Command */
    pid_t pid = fork();
    if(pid > 0){
        int pipe_idx = users[current_id].pipe_idx;
        int pipe_next_line =  cmd.pipeToCmd;
        /* child take pipe then close */
        if(users[current_id].ord_pipe[pipe_idx][0] >= 0){
            close(users[current_id].ord_pipe[pipe_idx][0]);
            close(users[current_id].ord_pipe[pipe_idx][1]);
            users[current_id].ord_pipe[pipe_idx][0] = -1;
            users[current_id].ord_pipe[pipe_idx][1] = -1;
            // cout << "Parent close ordinary pipe [" << pipe_idx << "] w/r" << endl;
        }

        // if after numbre > 1 maybe write sth into pipe, so cant close pipe write
        // afternumber :0/1 
        if(users[current_id].number_pipe[pipe_next_line][1] >= 0 && cmd.after_number <= 1){
            close(users[current_id].number_pipe[pipe_next_line][1]);
            users[current_id].number_pipe[pipe_next_line][1] = -1;
            // cout << "Parent close number pipe  [" << pipe_next_line << "] w" << endl;
        }

        if(cmd.isRecvUPipe){
            for(size_t i=0; i<userpipe.size(); i++){
                if(userpipe[i].fd[0] == cmd.Ufd_in){
                    close(userpipe[i].fd[0]);
                    close(userpipe[i].fd[1]);
                    userpipe.erase(userpipe.begin()+i);
                    cout << "close user pipe " << i << endl;
                    break;
                }
            }
        }

        usleep(100000);
        if (isSingleCommand || cmd.isNeedRedirect){
            int status;
            waitpid(pid, &status, 0); // WNOWAIT : leave the child in a waitable state; a later
                                      //              wait call can be used again retrieve the child status info.
        }else{
            int status;
            waitpid(-1, &status, WNOHANG);
        }
    }else if(pid == 0){
        dup2(users[current_id].ssock, STDIN_FILENO);
        dup2(users[current_id].ssock, STDOUT_FILENO);
        dup2(users[current_id].ssock, STDERR_FILENO);
        int pipe_next_line = cmd.pipeToCmd;
        int pipe_next_idx = users[current_id].pipe_next_idx;
        if (cmd.isNeedPipe || cmd.isErrPass){
            if(cmd.after_number == 0){ /* use ordinary pipe */
                dup2(users[current_id].ord_pipe[pipe_next_idx][1], STDOUT_FILENO);
                if(cmd.isErrPass) dup2(users[current_id].ord_pipe[pipe_next_idx][1], STDERR_FILENO);
                close(users[current_id].ord_pipe[pipe_next_idx][0]);
                close(users[current_id].ord_pipe[pipe_next_idx][1]);
                // cout << "Cild dup ordinary pipe " << pipe_next_idx << " to cout" << endl;
            }else{ /* use number pipe*/
                dup2(users[current_id].number_pipe[pipe_next_line][1], STDOUT_FILENO);
                if(cmd.isErrPass) dup2(users[current_id].number_pipe[pipe_next_line][1], STDERR_FILENO);
                close(users[current_id].number_pipe[pipe_next_line][0]);
                close(users[current_id].number_pipe[pipe_next_line][1]);
                // cout << "Cild dup number pipe " << pipe_next_line << " to cout" << endl;
            }
        }

        /* check previous number pipe*/
        int pipe_line = users[current_id].pipe_line;
        if(users[current_id].number_pipe[pipe_line][0] >= 0){
            dup2(users[current_id].number_pipe[pipe_line][0], STDIN_FILENO);
            close(users[current_id].number_pipe[pipe_line][0]);
            close(users[current_id].number_pipe[pipe_line][1]);
            // cout << "Cild dup number pipe" << pipe_line << " to cin" << endl;
        }

        /* check previous ordinary pipe*/
        int pipe_idx = users[current_id].pipe_idx;
        if(users[current_id].ord_pipe[pipe_idx][0] >= 0){
            dup2(users[current_id].ord_pipe[pipe_idx][0], STDIN_FILENO);
            close(users[current_id].ord_pipe[pipe_idx][0]);
            close(users[current_id].ord_pipe[pipe_idx][1]);
            // cout << "Cild dup ordinary pipe " << pipe_idx << " to cin" << endl;
        }

        if(cmd.isNeedRedirect){
            users[current_id].ord_pipe[pipe_next_idx][1] = open(cmd.passFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
            dup2(users[current_id].ord_pipe[pipe_next_idx][1], 1);
        }

        if(cmd.isRecvUPipe){
            dup2(cmd.Ufd_in, STDIN_FILENO);
        }

        if(cmd.isSendUPipe){
            dup2(cmd.Ufd_out, STDOUT_FILENO);
        }

        for(size_t i=0; i<userpipe.size(); i++){
            close(userpipe[i].fd[0]);
            close(userpipe[i].fd[1]);
        }

        // if(cmd.isUpipeError == false){
        //     Execute(cmd);
        // }
        Execute(cmd);
    }else{
        perror("fork error");
        exit(-1);
    }
    /* renew ordinary pipe index */
    users[current_id].pipe_idx = cmd.after_number == 0 ? (users[current_id].pipe_idx + 1) % N : 0;
    // cout << "Renew pipe_idx " <<  users[current_id].pipe_idx << endl;
    /* 
    * renew number pipe line
    * the last command of input or the command which has |number, then pipe_line should jump to next
    */
    if ( isLastCommand || cmd.after_number != 0) users[current_id].pipe_line = (users[current_id].pipe_line + 1) % N;
    // cout << "Renew pipe_line " << users[current_id].pipe_line << endl;
    signal(SIGCHLD, childHandler);
}

int main(int argc, char* argv[]) {
    if(argc != 2) {
        cout << "Need Port Number" << endl;
        return 0;
    }

    for (size_t i=0; i<MAX_USER; i++){
        users[i].Init();
    }

    int msock = CreateSocket((unsigned short)atoi(argv[1]));

    //int nfds = getdtablesize();
    current_id = 0;

    int nfds = msock+1;
    FD_ZERO(&activefds);
    FD_ZERO(&readfds);
    FD_SET(msock, &activefds);
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 500000;

    while(1) {
        memcpy(&readfds, &activefds, sizeof(readfds));

        for(size_t i=0; i<MAX_USER; i++){
            if( users[i].ssock > nfds) nfds = users[i].ssock; 
        }
        select(nfds+1, &readfds, NULL, NULL, &tv);

        if(FD_ISSET(msock, &readfds))ClientConnet(msock);

        for(size_t i=0; i<MAX_USER; i++){
            current_id = i;
            ClientHandle(i);
        }
    }
}