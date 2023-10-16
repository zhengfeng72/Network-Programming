#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;
using namespace std;

boost::asio::io_context io_context;

string temp, path;
class session : public std::enable_shared_from_this<session>{
public:
  session(tcp::socket socket) : socket_(std::move(socket)){}

  void start(){
    do_read();
  }

private:
  void do_read(){
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length){
          /* data_
          GET /panel.cgi HTTP/1.1
          Host: nplinux11.cs.nctu.edu.tw:7777
          Connection: keep-alive
          Cache-Control: max-age=0
          Upgrade-Insecure-Requests: 1
          User-Agent: .....
          Accept: ....
          Accept-Encoding: gzip, deflate
          Accept-Language: zh-TW,zh;q=0.9,en-US;q=0.8,en;q=0.7
          */
          
          sscanf(data_,"%s %s %s %s %s",REQUEST_METHOD, REQUEST_URI, SERVER_PROTOCOL, blackhole, HTTP_HOST);

          if (!ec){
            do_write(length);
          }
        });
  }

  void do_write(std::size_t length){
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(status_str, strlen(status_str)),
        [this, self](boost::system::error_code ec, std::size_t /*length*/){
          if (!ec){
            strcpy(SERVER_ADDR, socket_.local_endpoint().address().to_string().c_str());
            sprintf(SERVER_PORT, "%u", socket_.local_endpoint().port());
            strcpy(REMOTE_ADDR, socket_.remote_endpoint().address().to_string().c_str());
            sprintf(REMOTE_PORT, "%u", socket_.remote_endpoint().port());
            
            bool flag = false;
            int j=0;
            for(size_t i=0; i<1000; i++){
              if(REQUEST_URI[i] == '\0'){
                break;
              }

              if(REQUEST_URI[i] == '?'){
                flag = true;
                continue;
              }

              if(flag){
                QUERY_STRING[j] = REQUEST_URI[i];
                j++;
              }
            }

            for(size_t i=1; i<100; i++){
              if(REQUEST_URI[i] == '\0' || REQUEST_URI[i] == '?'){
                break;
              }

              EXEC_FILE[i+1] = REQUEST_URI[i];
            }

            cout << endl << "REQUEST_METHOD :[" << REQUEST_METHOD << "]" << endl;
            cout << "REQUEST_URI :[" << REQUEST_URI << "]" << endl;
            cout << "SERVER_PROTOCOL :[" << SERVER_PROTOCOL << "]" << endl;
            cout << "HTTP_HOST :[" << HTTP_HOST << "]" << endl;
            cout << "SERVER_ADDR :[" << SERVER_ADDR << "]" << endl;
            cout << "SERVER_PORT :[" << SERVER_PORT << "]" << endl;
            cout << "REMOTE_ADDR :[" << REMOTE_ADDR << "]" << endl;
            cout << "REMOTE_PORT :[" << REMOTE_PORT << "]" << endl;
            cout << "QUERY_STRING :[" << QUERY_STRING << "]" << endl;
            cout << "EXEC_FILE :[" << EXEC_FILE << "]" << endl;

            setenv("REQUEST_METHOD", REQUEST_METHOD, 1);
            setenv("REQUEST_URI", REQUEST_URI, 1);
            setenv("QUERY_STRING", QUERY_STRING, 1);
            setenv("SERVER_PROTOCOL", SERVER_PROTOCOL, 1);
            setenv("HTTP_HOST", HTTP_HOST, 1);
            setenv("SERVER_ADDR", SERVER_ADDR, 1);
            setenv("SERVER_PORT", SERVER_PORT, 1);
            setenv("REMOTE_ADDR", REMOTE_ADDR, 1);
            setenv("REMOTE_PORT", REMOTE_PORT, 1);
            setenv("EXEC_FILE", EXEC_FILE, 1);

            io_context.notify_fork(boost::asio::io_context::fork_prepare);
            if(fork() != 0){
              io_context.notify_fork(boost::asio::io_context::fork_parent);
              socket_.close();
            }else{
              io_context.notify_fork(boost::asio::io_context::fork_child);
              int sock = socket_.native_handle();
              dup2(sock, 0);
              dup2(sock, 1);
              dup2(sock, 2);
              socket_.close();
              if(execlp(EXEC_FILE, EXEC_FILE, NULL)<0){
                cerr << "exec error" << endl;
              }
            }

            do_read();
          }
        });
  }

  tcp::socket socket_;
  enum { max_length = 15000 };
  char data_[max_length];
  char status_str[200] = "HTTP/1.0 200 OK\n";
  char REQUEST_METHOD[10];
  char REQUEST_URI[1000];
  char QUERY_STRING[1000];
  char SERVER_PROTOCOL[100];
  char HTTP_HOST[1000];
  char SERVER_ADDR[80];
  char SERVER_PORT[10];
  char REMOTE_ADDR[80];
  char REMOTE_PORT[10];
  char EXEC_FILE[100] = "./";
  char blackhole[100];
};

class server{
public:
  server(short port) : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)){

    do_accept();
  }

private:
  void do_accept(){
    acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket){
        if (!ec){
            std::make_shared<session>(std::move(socket))->start();
        }

        do_accept();
    });
  }

  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]){
  try{
    if (argc != 2){
      std::cerr << "Usage: async_tcp_server <port>\n";
      return 1;
    }
    server s(std::atoi(argv[1]));

    io_context.run();
  }
  catch (std::exception& e){
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}