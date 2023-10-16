#include <iostream>
#include <string>
#include <filesystem>
#include <sstream>
#include <map>
#include <fstream>
#include <vector>
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/thread/thread.hpp>

using namespace std;
using boost::asio::ip::tcp;

string Cgi_server();
void ParseQuery(string);
string InitConsole(int);
boost::asio::io_context io_context;
map<string, string> parameters;
string temp, path;


struct Host{
    string host;
    string port;
    string file;
    bool used = false;
};

Host host_info[5];


class Client : public enable_shared_from_this<Client>{
    public:
        Client(shared_ptr<boost::asio::ip::tcp::socket> socket,int id) :
        socket_(socket),id_(id){
            file.open(("./test_case/"+host_info[id].file), ios::in);
        }

        void start(){
            do_resolve();
        }

    private:
        string do_replace(string content){
            boost::replace_all(content, "&", "&amp;");
            boost::replace_all(content, "\"", "&quot;");
            boost::replace_all(content, "'", "&apos;");
            boost::replace_all(content, " ", "&nbsp;");
            boost::replace_all(content, "<", "&lt;");
            boost::replace_all(content, ">", "&gt;");
            boost::replace_all(content, "\n", "<br/>");
            boost::replace_all(content, "\r", "");
            return content;
        }
        void do_resolve(){
            auto self(shared_from_this());
            boost::asio::ip::tcp::resolver::query query_(host_info[id_].host, host_info[id_].port);
            resolver_.async_resolve(query_, [this, self](const boost::system::error_code ec, boost::asio::ip::tcp::resolver::iterator i){
                if(!ec){
                    do_connect(i);
                }
            });
        }
        
        void do_connect(tcp::resolver::iterator i){
            auto self(shared_from_this());
            child_socket.async_connect(*i, [this, self](const boost::system::error_code ec){
                if(!ec){
                  // do_write("<script>document.getElementById('test').innerHTML += '<b>connect success</b><br/>';</script>");
                  do_read();
                }
            });
        }

        void do_read(){
            auto self(shared_from_this());
            // bzero(read_buffer, max_length);
            memset(read_buffer, '\0', max_length);
            child_socket.async_read_some(boost::asio::buffer(read_buffer, max_length), [this, self](const boost::system::error_code ec, size_t length){
                // cout << "<script>document.getElementById('test').innerHTML += '<b>do_read()</b><br/>';</script>" << endl;
                if(!ec){
                    content = "";
                    content.assign(read_buffer);
                    // bzero(read_buffer, max_length);
                    memset(read_buffer, '\0', max_length);
                    temp = do_replace(content);
                    do_write("<script>document.getElementById('s" + to_string(id_) + "').innerHTML += '" + temp + "';</script>");

                    //if(temp[temp.size()-2] == '%'){
                    if(temp.find("%")!= string::npos){
                        // cout << "<script>document.getElementById('test').innerHTML += '<b>do_write()</b><br/>';</script>" << endl;
                        do_write_cmd();
                    }else{
                        // cout << "<script>document.getElementById('test').innerHTML += '<b>content<br/> [" << temp <<"]</b><br/>';</script>" << endl;
                        do_read();
                    }
                }
                // else{
                    // do_write("<script>document.getElementById('test').innerHTML += '<b> do_read error " + ec.message() +"</b><br/>';</script>");
                // }
            });
        }

        void do_write_cmd(){
            auto self(shared_from_this());
            cmd = "";
            // do_write("<script>document.getElementById('test').innerHTML += '<b>do_write() getline() </b><br/>';</script>");
            getline(file, cmd);

            // do_write("<script>document.getElementById('test').innerHTML += '<b>" + cmd + "</b>';</script>");
            if(cmd.find("exit") != string::npos){
                file.close();
                host_info[id_].used = false;
            }
            cmd += "\n";
            temp = do_replace(cmd);
            
            do_write("<script>document.getElementById('s" + to_string(id_) + "').innerHTML += '<b>" + temp + "</b>';</script>");
            temp = "";

            async_write(child_socket, boost::asio::buffer(cmd.c_str(), cmd.size()), [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    if(host_info[id_].used == true) do_read();
                }
            });

        }

        void do_write(string content){
          auto self(shared_from_this());
          socket_->async_write_some(boost::asio::buffer(content.c_str(), content.size()),
              [this, self](boost::system::error_code ec, std::size_t /*length*/){
                if(!ec){}
              });
        }

        int id_;
        ifstream file;
        shared_ptr<boost::asio::ip::tcp::socket> socket_;
        boost::asio::ip::tcp::socket child_socket{io_context};
        boost::asio::ip::tcp::resolver resolver_{io_context};
        enum {max_length=10000};
        char read_buffer[max_length];
        string content = "";
        string cmd = "";
        string temp = "";
};


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
          /* parse http request */

          /* data_
            GET /panel.cgi HTTP/1.1
            Host: nplinux11.cs.nctu.edu.tw:7777
            Connection: keep-alive
            Control: max-age=0
            Upgrade-Insecure-Requests: 1
            User-Agent: ....
            ccept: ....
            Accept-Encoding: gzip, deflate
            Accept-Language: zh-TW,zh;q=0.9,en-US;q=0.8,en;q=0.7
+         */
          data_[length] = '\0';
          string httpReq(data_);
          int Index = httpReq.find('\n', 0);
          string r1 = httpReq.substr(0, Index-1);
          /* r1
          GET /panel.cgi HTTP/1.1
          */
         
          string r2 = httpReq.substr(Index+1, httpReq.find('\n', Index+1) - Index - 2);
          /* r2
          Host: nplinux11.cs.nctu.edu.tw:7777
          */

          int startIndex = 0;
          Index = r1.find(' ', 0);
          REQUEST_METHOD = r1.substr(0, Index);

          startIndex = Index + 1;
          Index = r1.find('?', startIndex);
          if(Index != -1){
            REQUEST_URI = r1.substr(startIndex, Index - startIndex); 
            temp = REQUEST_URI + "?";
            startIndex = Index + 1;
            Index = r1.find(' ', startIndex);
            QUERY_STRING = r1.substr(startIndex, Index - startIndex); //QUERY_STRING
            REQUEST_URI = REQUEST_URI + "?" + QUERY_STRING; //REQUEST_URI
          }else{
            Index = r1.find(' ', startIndex);
            QUERY_STRING = ""; //QUERY_STRING
            REQUEST_URI =  r1.substr(startIndex, Index - startIndex);  //REQUEST_URI
            temp = REQUEST_URI + "?";
          }
          //temp = REQUEST_URI + "?"; //be used in exec

          startIndex = Index + 1;
          SERVER_PROTOCOL = r1.substr(startIndex); //SERVER_PROTOCOL
          r2 = r2.substr(r2.find(' ', 0) + 1);
          HTTP_HOST = r2; //HTTP_HOST
          // SERVER_ADDR = r2.substr(0, r2.find(':', 0)); //SERVER_ADDR
          SERVER_PORT = r2.substr(r2.find(':', 0) + 1);	//SERVER_PORT
          SERVER_ADDR = socket_.local_endpoint().address().to_string();
          // SERVER_PORT = to_string(htons(socket_.local_endpoint().port()));
          REMOTE_ADDR = socket_.remote_endpoint().address().to_string();
          REMOTE_PORT = to_string(htons(socket_.remote_endpoint().port()));


          /*
          REQUEST_METHOD :[GET]
          REQUEST_URI :[/panel.cgi]
          SERVER_PROTOCOL :[HTTP/1.1]
          HTTP_HOST :[nplinux11.cs.nctu.edu.tw:7777]
          SERVER_ADDR :[140.113.235.234]
          SERVER_PORT :[7777]
          REMOTE_ADDR :
          REMOTE_PORT :[12227]
          QUERY_STRING :[]
          EXEC_FILE :[./panel.cgi]
          */
          cout << endl << "REQUEST_METHOD :[" << REQUEST_METHOD << "]" << endl;
          cout << "REQUEST_URI :[" << REQUEST_URI << "]" << endl;
          cout << "SERVER_PROTOCOL :[" << SERVER_PROTOCOL << "]" << endl;
          cout << "HTTP_HOST :[" << HTTP_HOST << "]" << endl;
          cout << "QUERY_STRING :[" << QUERY_STRING << "]" << endl;
          cout << "EXEC_FILE :[" << EXEC_FILE << "]" << endl;

          if (!ec){
            string req_uri(REQUEST_URI);
            if(req_uri.find("panel.cgi") != string::npos){
                cout << "******panel.cgi******" << endl;
                boost::asio::async_write(socket_, boost::asio::buffer(status_str, strlen(status_str)),
                  [this, self](boost::system::error_code ec, std::size_t /*length*/){
                    if (!ec){
                      do_panel();
                    }
                  });
            }else if(req_uri.find("console.cgi") != string::npos){
                cout << "******console.cgi******" << endl;
                boost::asio::async_write(socket_, boost::asio::buffer(status_str, strlen(status_str)),
                  [this, self](boost::system::error_code ec, std::size_t /*length*/){
                    if (!ec){
                      do_console();
                    }
                  });
            }else{
              socket_.close();
            }
          }
        });
  }

  void do_panel(){
    auto self(shared_from_this());
    string cgi_html = Cgi_server();
    boost::asio::async_write(socket_, boost::asio::buffer(cgi_html.c_str(), cgi_html.size()),
        [this, self](boost::system::error_code ec, std::size_t /*length*/){
            if(!ec){
                do_read();
            }
        });
  }

  void do_console(){
    auto self(shared_from_this());
    ParseQuery(QUERY_STRING);
    int host_num = parameters.size()/3;
    
    for(int i=0; i<host_num; i++){
        string host = "h" + to_string(i);
        string port = "p" + to_string(i);
        string file = "f" + to_string(i);
        host_info[i].host = parameters[host];
        host_info[i].port = parameters[port];
        host_info[i].file = parameters[file];
        host_info[i].used = true;
    }    

    string init_cosole_html = InitConsole(host_num);
    boost::asio::async_write(socket_, boost::asio::buffer(init_cosole_html.c_str(), init_cosole_html.size()),
        [this, self](boost::system::error_code ec, std::size_t /*length*/){
            if(!ec){}
        });

    shared_ptr<boost::asio::ip::tcp::socket> socket;
    socket = make_shared<boost::asio::ip::tcp::socket>(move(socket_));
    for(int i=0; i<5; i++){
      if(host_info[i].used){
        make_shared<Client>(socket, i)->start();
      }
    }
  }
  // void do_write(std::size_t length){
  //   auto self(shared_from_this());
  //   boost::asio::async_write(socket_, boost::asio::buffer(status_str, strlen(status_str)),
  //       [this, self](boost::system::error_code ec, std::size_t /*length*/){
  //         if (!ec){
            
  //         }
  //       });
  // }

  tcp::socket socket_;
  enum { max_length = 15000 };
  char data_[max_length];
  char status_str[200] = "HTTP/1.0 200 OK\n";
  string REQUEST_METHOD;
  string REQUEST_URI;
  string QUERY_STRING;
  string SERVER_PROTOCOL;
  string HTTP_HOST;
  string SERVER_ADDR;
  string SERVER_PORT;
  string REMOTE_ADDR;
  string REMOTE_PORT;
  string EXEC_FILE = "./";
};

class server{
public:
  server(short port) : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)){

    do_accept();
  }

private:
  void do_accept(){
    cout << "server accept" << endl;
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

string InitConsole(int num){
    string html = "Content-type: text/html\r\n\r\n";
    html = html + "<!DOCTYPE html> " +
            "<html lang=\"en\"> " +
            "<head> " +
            "<meta charset=\"UTF-8\" /> " +
            "<title>NP Project 3 Console</title> " +
            "<link " +
                "rel=\"stylesheet\" " +
                "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\" " +
                "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\" " +
                "crossorigin=\"anonymous\" " +
            "/> " +
            "<link " +
                "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\" " +
                "rel=\"stylesheet\" " +
            "/> " +
            "<link " +
                "rel=\"icon\" " +
                "type=\"image/png\" " +
                "href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\" " +
            "/> " +
            "<style> " +
                "* { " +
                    "font-family: 'Source Code Pro', monospace; " +
                    "font-size: 1rem !important; " +
                "} " +
                "body { " +
                    "background-color: #212529; " +
                "} " +
                "pre { " +
                    "color: #cccccc; " +
                "} " +
                "b { " +
                    "color: #01b468; " +
                "} " +
            "</style> " +
            "</head> " +
            "<body> " +
            "<table class=\"table table-dark table-bordered\"> " +
            "<thead> " +
                "<tr> ";
    for(int i=0; i<num; i++){
        string host = "h" + to_string(i);
        string port = "p" + to_string(i);
        html = html + "<th scope=\"col\"> " + parameters[host] + ":" + parameters[port] + "</th> ";
    }
        html = html + "</tr> " +
            "</thead> " +
            "<tbody> " +
            "<tr> ";
    for(int i=0; i<num; i++){
        html = html + "<td><pre id=\"s" + to_string(i) + "\" class=\"mb-0\"></pre></td> ";
    }
        html = html + "</tr> " + "</tbody> " + "</table> " + 
        // "<div id=\"test\" style=\"padding:10px;backgroound-color:white;color:white\">test<br/></div>" +
        "</body> " + "</html>";
    return html;
}

void ParseQuery(string query){
    //QUERY_STRING :[h0=nplinux11.cs.nctu.edu.tw&p0=1235&f0=t1.txt&h1=&p1=&f1=&h2=&p2=&f2=&h3=&p3=&f3=&h4=&p4=&f4=]
    // host port file
    istringstream ss(query);

    string key_val, key, val;
    while(getline(ss, key_val, '&')){
        size_t found = key_val.find("=");
        if(found == key_val.length()-1) break;
        key = key_val.substr(0, found);
        val = key_val.substr(found+1);
        parameters[key] = val;
    }
}

string Cgi_server(){
    string test_case_menu;
    string host_menu;
    string FORM_METHOD = "GET";
    string FORM_ACTION = "console.cgi";
    for(size_t i=1; i<6; i++){
        test_case_menu += "<option value=\"t" + to_string(i) + ".txt\">t" + to_string(i) + ".txt</option>";
    }

    for(int i=1; i<13; i++){
        host_menu += "<option value=\"nplinux" + to_string(i) + ".cs.nctu.edu.tw\">nplinux" + to_string(i) + "</option>";
        // host_menu += "<option value=\"localhost\">localhost</option>";
    }

    string cgi_html = "Content-type: text/html\r\n\r\n";
    cgi_html =
    cgi_html + 
    "<!DOCTYPE html>" +
    "<html lang=\"en\">" +
    "<head>" +
        "<title>NP Project 3 Panel</title> " +
        "<link " +
        "rel=\"stylesheet\" " +
        "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\" " +
        "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\" " +
        "crossorigin=\"anonymous\" "+
        "/>" +
        "<link " +
        "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\" " +
        "rel=\"stylesheet\" " +
        "/>" +
        "<link " +
        "rel=\"icon\" " +
        "type=\"image/png\" "+
        "href=\"https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png\" " +
        "/>" +
        "<style>" +
        "* { "+
        "    font-family: 'Source Code Pro', monospace;" +
        "}" +
        "</style>" +
    "</head>" +
    "<body class=\"bg-secondary pt-5\">" +
    "<form action=\"" + FORM_ACTION +"\" method=\""+ FORM_METHOD+"\">" +
        "<table class=\"table mx-auto bg-light\" style=\"width: inherit\">" +
        "<thead class=\"thead-dark\">" +
          "<tr>" +
            "<th scope=\"col\">#</th>" +
            "<th scope=\"col\">Host</th>" +
            "<th scope=\"col\">Port</th>" +
            "<th scope=\"col\">Input File</th>" +
          "</tr>" +
        "</thead>" +
        "<tbody>";
    
    for(int i=0; i<5; i++){
        cgi_html = 
        cgi_html +
        "<tr>" +
            "<th scope=\"row\" class=\"align-middle\">Session " + to_string(i + 1) + "</th>" +
            "<td>" +
              "<div class=\"input-group\">" +
                "<select name=\"h" + to_string(i) + "\" class=\"custom-select\">" +
                  "<option></option>" + host_menu +
                "</select>" +
                "<div class=\"input-group-append\">" +
                  "<span class=\"input-group-text\">.cs.nctu.edu.tw</span>" +
                "</div>" +
              "</div>" +
            "</td>" +
            "<td>" +
              "<input name=\"p" + to_string(i) + "\" type=\"text\" class=\"form-control\" size=\"5\" />" +
            "</td>" +
            "<td>" +
              "<select name=\"f" + to_string(i) +"\" class=\"custom-select\">" +
                "<option></option>" +
                test_case_menu +
              "</select>" +
            "</td>" +
          "</tr>";
    }
    cgi_html = cgi_html + "<tr>" +
                "<td colspan=\"3\"></td>" +
                "<td>" +
                "<button type=\"submit\" class=\"btn btn-info btn-block\">Run</button>" +
                "</td>" +
            "</tr>" +
            "</tbody>" +
        "</table>"+
        "</form>"+
    "</body>"+
    "</html>";
    return cgi_html;
}

// vector<string> GetFiles(){
//     DIR *dr;
//     struct dirent *en;
//     dr = opendir("./test_case");
//     string file;
//     vector<string> files;
//     if(dr){
//         while((en = readdir(dr))!=NULL){
//             if(en->d_type==DT_REG){
//                 file="";
//                 file.assign(en->d_name);
//                 cout << file << endl;
//                 files.push_back(file);
//             }
//                 // printf("%hhd %s\n",en->d_type, en->d_name);
//         }
//     }
//     closedir(dr);

//     sort(files.begin(), files.end());

//     return files;
// }