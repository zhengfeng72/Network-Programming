#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>
#include <fstream>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

using boost::asio::ip::tcp;
using namespace std;

bool check_client_firewall();
boost::asio::io_context io_Context;
string src_addr;
string src_port;
string dst_addr;
string dst_port;
string domain_name;
string user_id;


void handler(int sig){
    int status;
    while(waitpid(-1, &status, WNOHANG)>0){}
}

class sock_info{
public:
    unsigned char VN;
    unsigned char CD;
    unsigned char DST_PORT[2];
    unsigned char DST_IP[4];
    int state;

    array<unsigned char, 8> do_reply(int cd, int status){
        array<unsigned char, 8> message;
        if(status == 90) /* accept */
            message[1] = (unsigned char)90;
        else if(status == 91) /* reject */
            message[1] = (unsigned char)91;

        if(cd == 1){ /* connect */
            message[0] = (unsigned char)0;
            for(int i=2 ; i<8 ; i++){
                message[i] = (unsigned char)0;
            }
        }else if(cd == 2){ /* bind */
            message[0] = (unsigned char)0;
            message[2] = DST_PORT[0];
            message[3] = DST_PORT[1];
            message[4] = DST_IP[0];
            message[5] = DST_IP[1];
            message[6] = DST_IP[2];
            message[7] = DST_IP[3];
        }
        return message;
    }

    void do_print(){
        cout << "------------------------------------------" << endl;
        cout << "<S_IP>: " << src_addr << endl;
        cout << "<S_PORT>: " << src_port << endl;
        cout << "<D_IP>: " << dst_addr << endl;
        cout << "<D_PORT>: " << dst_port << endl;
        
        cout << "<Command>: ";
        if (CD == 1) cout <<"CONNECT";
        else if (CD == 2) cout << "BIND";
        cout << endl;
        
        cout << "<Reply>: ";
        if(state == 90) cout << "Accept";
        else if (state == 91) cout << "Reject";
        cout<<endl;

        cout << "------------------------------------------" << endl << endl;
    }

    void do_check(){
        ifstream file;
        file.open("./socks.conf");

        state = 91;
        string command = "";
        if(file){
            if(CD == 1) /* connect */
                command ="c ";
            else if(CD == 2) /* bind */
                command = "b ";

            string line;
            while(getline(file, line)){
                if(line.find(command) != string::npos){
                    stringstream s(line);
                    string event, type, ip;
                    s >> event >> type >> ip;

                    vector<string> ip_v;
                    vector<string> dstip_v;
                    boost::split(ip_v, ip, boost::is_any_of("."), boost::token_compress_on);
                    boost::split(dstip_v, dst_addr, boost::is_any_of("."), boost::token_compress_on);

                    bool f1 = false, f2 = false, f3 = false, f4 = false;
                    if( (ip_v[0] == "*") || (ip_v[0] == dstip_v[0]) )
                        f1 = true;
                    if( (ip_v[1] == "*") || (ip_v[1] == dstip_v[1]) )
                        f2 = true;
                    if( (ip_v[2] == "*") || (ip_v[2] == dstip_v[2]) )
                        f3 = true;
                    if( (ip_v[3] == "*") || (ip_v[3] == dstip_v[3]) )
                        f4 = true;
                    if( f1 && f2 && f3 && f4){
                        state = 90;
                        break;
                    }
                }
            }
            file.close();
        }else{
            state = 91;
            cerr << "can't open socks.conf" << endl;
        }
    }
};


class connect_s : public std::enable_shared_from_this<connect_s>{
public:
    connect_s(sock_info sockInfo, tcp::socket socket) : 
    sock_(sockInfo),socket_src(std::move(socket)){}

    void start(){
        do_resolve();
    }

    void do_send_src(size_t length){
        auto self(shared_from_this());
        async_write(socket_src, boost::asio::buffer(dst_data_, length),
            [this, self](const boost::system::error_code ec, size_t length_sec){
            if(!ec){
                do_read_dst();
            }
        });
    }

    void do_read_dst(){
        auto self(shared_from_this());
        bzero(dst_data_, max_length);
        socket_.async_read_some( boost::asio::buffer(dst_data_),
            [this, self](const boost::system::error_code ec, size_t length){
            if(!ec){
                do_send_src(length);
            }
        });
    }

    void do_send_dst(size_t length){
        auto self(shared_from_this());

        async_write(socket_, boost::asio::buffer(src_data_, length),
            [this, self](const boost::system::error_code ec, size_t length){
            if(!ec){
                do_read_src();
            }
        });
    }

    void do_read_src(){
        auto self(shared_from_this());

        bzero(src_data_, max_length);
        socket_src.async_read_some(boost::asio::buffer(src_data_), [this, self](const boost::system::error_code ec, size_t length){
            if(!ec){
                do_send_dst(length);
            }
        });
    }

    void do_connect_handler(){
        auto self(shared_from_this());

        message = sock_.do_reply(1, 90); /* connect, accept */
        async_write(socket_src, boost::asio::buffer(message.data(), message.size()),
            [this, self](const boost::system::error_code ec, size_t length){
            if(!ec){
                do_read_src();
                do_read_dst();
            }
        });
    }

    void do_connect(boost::asio::ip::tcp::resolver::iterator it){
        auto self(shared_from_this());
        socket_.async_connect(*it, [this, self](const boost::system::error_code ec){
            if(!ec){
                do_connect_handler();
            }
        });
    }


    void do_resolve(){
        auto self(shared_from_this());
        boost::asio::ip::tcp::resolver::query res_que(dst_addr, dst_port);
        resolver_.async_resolve(res_que, [this, self](const boost::system::error_code ec, boost::asio::ip::tcp::resolver::iterator it){
            if(!ec){
                do_connect(it);
            }
        });
    }

private:
    sock_info sock_;
    tcp::socket socket_src;
    tcp::socket socket_{io_Context};
    tcp::resolver resolver_{io_Context};
    enum { max_length = 15000 };
    char src_data_[max_length];
    char dst_data_[max_length];
    array<unsigned char, 8> message;
};

class bind_s : public std::enable_shared_from_this<bind_s>{
public:
    bind_s(sock_info sockInfo, tcp::socket socket) :
    sock_(sockInfo),socket_src(std::move(socket)), acceptor_(io_Context, tcp::endpoint(tcp::v4(), 0)){}

    void do_send_src(size_t length){
        auto self(shared_from_this());
        async_write(socket_src, boost::asio::buffer(dst_data_, length), [this, self](const boost::system::error_code ec, size_t length){
            if(!ec){
                do_read_dst();
            }
        });
    }

    void do_send_dst(size_t length){
        auto self(shared_from_this());
        async_write(socket_, boost::asio::buffer(src_data_, length), [this, self](const boost::system::error_code ec, size_t length){
            if(!ec){
                do_send_src(length);
            }else{
                socket_src.close();
                socket_.close();
            }
        });
    }

    void do_read_src(){
        auto self(shared_from_this());
        bzero(src_data_, max_length);
        socket_src.async_read_some(boost::asio::buffer(src_data_),[this, self](const boost::system::error_code ec, size_t length){
            if(!ec){
                do_send_dst(length);
            }else{
                socket_src.close();
                socket_.close();
            }
        });
    }

    void do_read_dst(){
        auto self(shared_from_this());
        bzero(dst_data_, max_length);
        socket_.async_read_some(boost::asio::buffer(dst_data_), [this, self](const boost::system::error_code ec, size_t length){
            if(!ec){
                do_read_src();
            }
        });
    }


    void do_send_sec(){
        auto self(shared_from_this());
        string dest_ip = socket_.remote_endpoint().address().to_string();
        port = socket_.local_endpoint().port();

        message_sec[0] = (unsigned char)0;
        message_sec[1] = (unsigned char)0x5A;
        message_sec[2] = (unsigned char)(port/256);
        message_sec[3] = (unsigned char)(port%256);
        for(int i=4; i<8; i++) message_sec[i] = (unsigned char)0;

        async_write(socket_src, boost::asio::buffer(message_sec.data(), message_sec.size()), [this, self](const boost::system::error_code ec, size_t length){
            if(!ec){
                do_read_dst();
                do_read_src();
            }
        });
    }

    void do_send_first(){
        auto self(shared_from_this());

        port = acceptor_.local_endpoint().port();
        message[0] = (unsigned char)0;
        message[1] = (unsigned char)0x5A;
        message[2] = (unsigned char)(port/256);
        message[3] = (unsigned char)(port%256);
        for(int i=4; i<8; i++) message[i] = (unsigned char)0;
    
        async_write(socket_src, boost::asio::buffer(message.data(), message.size()), [this, self](const boost::system::error_code ec, size_t length){
            if(!ec){}
        });
    }

    void do_accept(){
        auto self(shared_from_this());
        acceptor_.async_accept(socket_,[self, this](boost::system::error_code ec){
            if(!ec){
                do_send_sec();
            }
        });
    }

    void start(){
        do_accept();
        do_send_first();
    }

private:
    sock_info sock_;
    tcp::socket socket_src;
    tcp::socket socket_{io_Context};
    tcp::resolver resolver_{io_Context};
    tcp::acceptor acceptor_;
    unsigned int port;
    array<unsigned char, 8> message;
    array<unsigned char, 8> message_sec;
    enum {max_length = 15000};
    char src_data_[max_length];
    char dst_data_[max_length];
};

class session : public enable_shared_from_this<session>{
public:
    session(tcp::socket socket): socket_(move(socket)){}

    void do_handler(){
        auto self(shared_from_this());
        src_addr = socket_.remote_endpoint().address().to_string();
        src_port = to_string(socket_.remote_endpoint().port());

        sock_info s;
        
        s.VN = data_[0]; //version
        
        s.CD = data_[1]; //command
        
        s.DST_PORT[0] = data_[2]; //port
        s.DST_PORT[1] = data_[3];
        
        s.DST_IP[0] = data_[4]; //ip
        s.DST_IP[1] = data_[5];
        s.DST_IP[2] = data_[6];
        s.DST_IP[3] = data_[7];
        dst_addr = to_string((int)s.DST_IP[0]) + "." + to_string((int)s.DST_IP[1]) + "." + to_string((int)s.DST_IP[2]) + "." + to_string((int)s.DST_IP[3]);
        dst_port = to_string((int)s.DST_PORT[0]*256 + (int)s.DST_PORT[1]);
        string USERID(data_ + 8);

        //if IP == 0.0.0.X
        if((int)s.DST_IP[0] == 0 && (int)s.DST_IP[1] == 0 && (int)s.DST_IP[2] == 0){
            tcp::resolver resolver(io_Context);
            tcp::resolver::query que(domain_name, "");
            tcp::resolver::iterator it;
            domain_name.assign(data_ + 8 + USERID.length() + 1);
            for(it = resolver.resolve(que); it != tcp::resolver::iterator(); ++it){
                tcp::endpoint ep = *it;
                if(ep.address().is_v4())
                    dst_addr = ep.address().to_string();
            }
        }

        // check VN
        if(s.VN != 4){
            return;
        }

        s.do_check();

        // if(check_client_firewall()){
        //     if(s.state == 90){
        //         if(s.CD == 1){ /* connect */
        //             s.do_print();
        //             make_shared<connect_s>(s, move(socket_))->start();
        //         }else if(s.CD == 2){ /* bind */
        //             s.do_print();
        //             make_shared<bind_s>(s, move(socket_))->start();
        //         }
        //     }else if(s.state == 91){
        //         if(s.CD == 1 ) /* connect */
        //             s.do_print();
        //         else if(s.CD == 2) /* bind */
        //             s.do_print();
        //         array<unsigned char, 8> message = s.do_reply(s.CD, s.state);

        //         socket_.async_write_some(boost::asio::buffer(message.data(),message.size()),
        //             [this, self](const boost::system::error_code ec, size_t length){
        //         });
        //     }
        // }else{
        //     s.state = 91;
        //     s.do_print();

        //     array<unsigned char, 8> message = s.do_reply(s.CD, s.state);

        //     socket_.async_write_some(boost::asio::buffer(message.data(),message.size()),
        //         [this, self](const boost::system::error_code ec, size_t length){
        //     });
        // }

        if(s.state == 90){
            if(s.CD == 1){ /* connect */
                s.do_print();
                make_shared<connect_s>(s, move(socket_))->start();
            }else if(s.CD == 2){ /* bind */
                s.do_print();
                make_shared<bind_s>(s, move(socket_))->start();
            }
        }else if(s.state == 91){
            if(s.CD == 1 ) /* connect */
                s.do_print();
            else if(s.CD == 2) /* bind */
                s.do_print();
            array<unsigned char, 8> message = s.do_reply(s.CD, s.state);

            socket_.async_write_some(boost::asio::buffer(message.data(),message.size()),
                [this, self](const boost::system::error_code ec, size_t length){
            });
        }
    }

    void do_read(){
        auto self(shared_from_this());
        bzero(data_, max_length);
        socket_.async_read_some(boost::asio::buffer(data_, max_length), [this, self](const boost::system::error_code ec, size_t length){
            if(!ec){
                do_handler();
            }
        });
    }

    void start(){
        do_read();
    }

private:
    tcp::socket socket_;
    enum {max_length = 15000};
    char data_[max_length];
};

class server{
public:
    server(short port) : acceptor_(io_Context, tcp::endpoint(tcp::v4(), port)){
        do_accept();
    }

    void do_accept(){
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket){
            if (!ec){
                io_Context.notify_fork(boost::asio::io_service::fork_prepare);

                pid_t pid = fork();
                if(pid == 0){ /* child */
                    io_Context.notify_fork(boost::asio::io_service::fork_child);                        
                    make_shared<session>(move(socket))->start();
                }else{ /* parent */
                    io_Context.notify_fork(boost::asio::io_service::fork_parent);
                    socket.close();
                    do_accept();
                } 
            }
        });
    }
private:
    tcp::acceptor acceptor_;
};

bool check_client_firewall(){
    ifstream file;
    file.open("./client_socks.conf");

    bool state = false;
    string command = "";
    if(file){
        command ="c ";

        string line;
        while(getline(file, line)){
            if(line.find(command) != string::npos){
                stringstream s(line);
                string event, type, ip;
                s >> event >> type >> ip;

                vector<string> ip_v;
                vector<string> srcip_v;
                boost::split(ip_v, ip, boost::is_any_of("."), boost::token_compress_on);
                boost::split(srcip_v, src_addr, boost::is_any_of("."), boost::token_compress_on);

                bool f1 = false, f2 = false, f3 = false, f4 = false;
                if( (ip_v[0] == "*") || (ip_v[0] == srcip_v[0]) )
                    f1 = true;
                if( (ip_v[1] == "*") || (ip_v[1] == srcip_v[1]) )
                    f2 = true;
                if( (ip_v[2] == "*") || (ip_v[2] == srcip_v[2]) )
                    f3 = true;
                if( (ip_v[3] == "*") || (ip_v[3] == srcip_v[3]) )
                    f4 = true;
                if( f1 && f2 && f3 && f4){
                    state = true;
                    break;
                }
            }
        }
        file.close();
    }else{
        state = false;
        cerr << "can't open socks.conf" << endl;
    }
    return state;
}


int main(int argc, char* argv[]){
    try{
        if (argc != 2)return 1;

        signal(SIGCHLD, handler);
        server s(atoi(argv[1]));
        io_Context.run();
    }catch (exception& e){
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}