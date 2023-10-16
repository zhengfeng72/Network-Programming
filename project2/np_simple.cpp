#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <string>
#include <cstring>
#include <vector>
#include <unistd.h> //pipe dup2
#include <fcntl.h> //open
#include <sstream>
#define N 1001

using namespace std;
struct Cmd_Info
{
    string name;
    vector<string> arg;
    int after_number = 0; // jump number of line
    bool isNeedPipe = false;
    bool isNeedRedirect = false; // >
    string passFile;
    bool isErrPass = false; // !number
    /*
    only pipe :
        isNeedPipe = true
        after_number =0
    number pipe:
        isNeedPipe = true
        after_number = number
    redirect
        isNeedRedirect = true
    */
};

vector<string> Split(string str);
void Execute(Cmd_Info);
Cmd_Info Convert(vector<string>, bool);
void Print(vector<Cmd_Info>);
void childHandler(int);
int np_shell();

int main(int argc, char* argv[]) {
    int sockfd, newsockfd, childpid;
    socklen_t clilen;
    sockaddr_in cli_addr, serv_addr;
    unsigned int port = 7001;
    if (argc == 2) {
        port = (unsigned int)atoi(argv[1]);
    }

    if (( sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        cerr << "Socket create fail" << endl;
        exit(0);
    }

    /*
    * bind our local address so that the client can send to us.
    */
   bzero((char *)&serv_addr, sizeof(serv_addr));
   serv_addr.sin_family = AF_INET;
   serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
   serv_addr.sin_port = htons(port);

    if (bind(sockfd, (sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        cerr << "Server: cant bind local address." << strerror(errno) << endl;
        exit(0);
    }

    listen(sockfd, 1);

    cout << "Server Start" << endl;
    while(true) {
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
            cerr << "Accept error" << endl;
            exit(0);
        }

        childpid = fork();
        if (childpid == 0) { /*Child process*/
            dup2(newsockfd, 0);
            dup2(newsockfd, 1);
            dup2(newsockfd, 2);
            close(sockfd);
            np_shell();
            break;
        }else {
            close(newsockfd);
            signal(SIGCHLD, childHandler);
        }
    }
    return 0;
}

int np_shell()
{
    int fd[N][N][2];
    // reset
    for (size_t i = 0; i < N; i++)
    {
        for (size_t j=0; j<N; j++)
        {
            fd[i][j][0] = -1;
            fd[i][j][1] = -1;
        }
    }

    int pipe_idx = 0;
    int pipe_next_idx = -1;
    int pipe_line = 0;
    setenv("PATH", "bin:.", 1);

    while (true)
    {
        cout << "% ";
        string input;
        getline(cin, input);

        if (input == "") continue;

        vector<string> tokens = Split(input);

        // turn input to struct vector
        vector<Cmd_Info> cmd_vector;

        vector<string> tmp_vector_str; // contain cmd name arg |numer which split from input
        for (size_t i = 0; i < tokens.size(); i++)
        {
            // tokens find '|' ...
            if (tokens[i].find('|') != tokens[i].npos || tokens[i].find('!') != tokens[i].npos)
            {
                tmp_vector_str.push_back(tokens[i]);
                Cmd_Info tmp_cmd = Convert(tmp_vector_str, true);
                tmp_vector_str.clear();
                cmd_vector.push_back(tmp_cmd);
            }
            else
            {
                tmp_vector_str.push_back(tokens[i]);
            }
        }
        // don't include '|' '!'
        if (!tmp_vector_str.empty())
        {
            Cmd_Info tmp_cmd = Convert(tmp_vector_str, false);
            tmp_cmd.after_number = 1;
            cmd_vector.push_back(tmp_cmd);
        }

        //test 
        // Print(cmd_vector);

        // special command
        if (cmd_vector[0].name == "printenv")
        {
            if (getenv(cmd_vector[0].arg[0].c_str()) != NULL)
            {
                string env = getenv(cmd_vector[0].arg[0].c_str());
                cout << env << endl;
            }
            continue;
        }

        if (cmd_vector[0].name == "setenv")
        {
            setenv(cmd_vector[0].arg[0].c_str(), cmd_vector[0].arg[1].c_str(), 1);
            continue;
        }

        if (cmd_vector[0].name == "exit")
        {
            return 0;
        }

        pid_t pid;

        for (size_t i = 0; i < cmd_vector.size(); i++)
        {
            // set pipe
            // cout << endl << "Execute Command " << i << endl;
            pipe_next_idx = cmd_vector[i].after_number == 0 ?(pipe_idx + 1) % N :0;
            int pipe_next_line = (pipe_line + cmd_vector[i].after_number) % N;

            // test print
            // cout << "cur_pipe [" << pipe_line << "] [" << pipe_idx << "]" << endl;
            // cout << "next_pipe [" << pipe_next_line << "] [" << pipe_next_idx << "]" << endl;
            if (cmd_vector[i].isNeedPipe || cmd_vector[i].isErrPass)
                {
                    if (fd[pipe_next_line][pipe_next_idx][0] < 0)
                    {
                        pipe(fd[pipe_next_line][pipe_next_idx]);
                        // cout << "main create pipe " << pipe_next_line << " " << pipe_next_idx << endl;
                    }
                }

            // pid > 0 : parant process
            // pid = 0 : child process
            pid = fork();
            
            if (pid > 0)
            {
                

                if ( fd[pipe_line][pipe_idx][0] >= 0)
                {
                    close( fd[pipe_line][pipe_idx][0] );
                    close( fd[pipe_line][pipe_idx][1] );
                    fd[pipe_line][pipe_idx][0] = -1;
                    fd[pipe_line][pipe_idx][1] = -1;
                    // cout << "close [" << pipe_line << "][" << pipe_idx << "] R/W" << endl;
                }

                // if after numbre > 1 maybe write sth into pipe, so cant close pipe write
                if ( fd[pipe_next_line][pipe_next_idx][1] >= 0 && cmd_vector[i].after_number <= 1){
                    close( fd[pipe_next_line][pipe_next_idx][1]);
                    fd[pipe_next_line][pipe_next_idx][1] = -1;
                    // cout << "close [" << pipe_next_line << "][" << pipe_next_idx << "] W" << endl;
                }
                usleep(50000);
                if (cmd_vector.size()==1 || cmd_vector[i].isNeedRedirect)
                {
                int status;
                waitpid(pid, &status, 0); // WNOWAIT : leave the child in a waitable state; a later
                                            //              wait call can be used again retrieve the child status info.
                }
            }
            else if (pid == 0)
            {
                // cout << "pid " << getpid() << endl;
                // cout << cmd_vector[i].isNeedPipe << " " << cmd_vector[i].isErrPass  << endl;
                if ( cmd_vector[i].isNeedPipe || cmd_vector[i].isErrPass )
                {
                    dup2(fd[pipe_next_line][pipe_next_idx][1], 1);

                    if ( cmd_vector[i].isErrPass )
                    {
                        dup2(fd[pipe_next_line][pipe_next_idx][1], 2);
                    }

                    // close child #3 #4
                    close(fd[pipe_next_line][pipe_next_idx][0]);
                    close(fd[pipe_next_line][pipe_next_idx][1]);
                }

                // if cur pipe have sth, so stdin instead of pipe read
                if ( fd[pipe_line][pipe_idx][0] >= 0 )
                {
                    dup2(fd[pipe_line][pipe_idx][0], 0);
                    // close child #3 #4
                    close(fd[pipe_line][pipe_idx][0]);
                    close(fd[pipe_line][pipe_idx][1]);
                }

                if (cmd_vector[i].isNeedRedirect)
                {
                    // redirect file name is next command name;
                    fd[pipe_next_line][pipe_next_idx][1] = open(cmd_vector[i].passFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
                    dup2(fd[pipe_next_line][pipe_next_idx][1], 1);
                    /*
                    O_WRONLY : use write mode to open file
                    O_CREAT  : if doesnt exit file, then create file
                    O_TRUNC  : if exit file, then it will truncate to length 0
                    */
                }

                Execute(cmd_vector[i]);
            }
            else
            {
                perror("fork error");
                exit(-1);
            }
            pipe_idx = cmd_vector[i].after_number == 0 ? (pipe_idx + 1) % N : 0;

            // the last command of input or the command which has |number, then pipe_line should jump to next
            if ( i == cmd_vector.size()-1 || cmd_vector[i].after_number != 0) pipe_line = (pipe_line + 1) % N;
            signal(SIGCHLD, childHandler);
        }
    }
    return 0;
}

Cmd_Info Convert(vector<string> tmp_vector_string, bool isInclude)
{
    /*
        vector string:
            begin = cmd.name
            middle elements = cmd.arg
            end = check after_number isNeedPipe isErrPass
    */
    Cmd_Info tmp_cmd;
    tmp_cmd.name = tmp_vector_string[0];
    int num = tmp_vector_string.size();
    // include | !
    if (isInclude)
    {
        string last = tmp_vector_string[num - 1];
        char type = last[0];

        string number = last.erase(0, 1);
        if (!number.empty())
        {
            tmp_cmd.after_number = stoi(number);
        }

        tmp_vector_string.erase(tmp_vector_string.begin());
        tmp_vector_string.erase(tmp_vector_string.end());

        if (type == '|')
            tmp_cmd.isNeedPipe = true;
        else if (type == '!')
        {
            tmp_cmd.isErrPass = true;
            tmp_cmd.isNeedPipe = true;
        }
    }
    else
    {
        tmp_vector_string.erase(tmp_vector_string.begin());
        tmp_cmd.isNeedPipe = false;
        tmp_cmd.isErrPass = false;
    }

    // set redirect
    for (size_t i = 0; i < tmp_vector_string.size(); i++)
    {
        if (tmp_vector_string[i] == ">")
        {
            tmp_cmd.isNeedRedirect = true;
            tmp_cmd.passFile = tmp_vector_string[i + 1];
            tmp_vector_string.erase(tmp_vector_string.begin() + i + 1);
            tmp_vector_string.erase(tmp_vector_string.begin() + i);
        }
    }
    tmp_cmd.arg = tmp_vector_string;
    return tmp_cmd;
}

void Execute(Cmd_Info cmd)
{
    // besides args, execvp args need command name
    // command name put head, NULL put tail
    char *cus_argv[cmd.arg.size() + 2];

    // put command name into args[0]
    cus_argv[0] = new char(cmd.name.size() + 1);
    strcpy(cus_argv[0], cmd.name.c_str());

    // put other args into args[1~ end-1]
    if (!cmd.arg.empty())
    {
        for (size_t i = 1; i <= cmd.arg.size(); i++)
        {
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

vector<string> Split(string str){
    vector<string> res;
    res.clear();
    istringstream ss(str);
    string s;
    while(ss>>s){
        res.push_back(s);
    }
    return res;
}

void Print(vector<Cmd_Info> cmd)
{
    for (size_t i = 0; i < cmd.size(); i++)
    {
        cout << "command " << i << endl;
        cout << "name : [" << cmd[i].name << "]" << endl;
        cout << "arg : ";
        for (size_t j = 0; j < cmd[i].arg.size(); j++)
        {
            cout << "[" << cmd[i].arg[j] << "]";
        }
        cout << endl;
        cout << "after number : " << cmd[i].after_number << endl;
        cout << "isNeedPipe : " << cmd[i].isNeedPipe << endl;
        cout << "isNeedRedirect : " << cmd[i].isNeedRedirect << endl;
        cout << "passFile : " << cmd[i].passFile << endl;
        cout << "isErrPass : " << cmd[i].isErrPass << endl;
        cout << "-------------------" << endl;
    }
}

void childHandler(int signo)
{
    /*
    let the parant process know the child process has terminated
    */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0){};
}