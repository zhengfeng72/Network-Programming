#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <fstream>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

using namespace std;
using namespace boost::asio::ip;


map<string, string> parameters;
string socks_host;
string socks_port;

void ParseQuery();
void InitConsole(int);

struct Host{
    string host;
    string port;
    string file;
    bool used = false;
};

Host host_info[5];
boost::asio::io_context io_context;

class Client : public enable_shared_from_this<Client>{
    public:
        Client(int id) :
        id_(id){
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
            socket_.async_connect(*i, [this, self](const boost::system::error_code ec){
                if(!ec){
                    do_send();
                }
            });
        }

        void do_send(){
            auto self(shared_from_this());
            message = "";
            message += (char)4; //version
            message += (char)1; //command
            message += (char)(stoi(host_info[id_].port)/256);
            message += (char)(stoi(host_info[id_].port)%256);

            message += (char)0; //dst ip
            message += (char)0;
            message += (char)0;
            message += (char)1;

            message += (char)0; // user id / null

            message += host_info[id_].host; // domain name
            message += (char)0; //null

            // tcp::resolver res(io_context);
            // tcp::resolver::query q(host_info[stoi(id_)].host, "");
            // for(tcp::resolver::iterator it = res.resolve(q); it != tcp::resolver::iterator(); ++it){
            //     tcp::endpoint ep = *it;
            //     if(ep.address().is_v4()) 
            // }
            tcp::resolver::query q(host_info[id_].host, "");
            tcp::resolver::iterator it = resolver_.resolve(q);
            tcp::endpoint endpoint = *it;
            socket_.connect(endpoint);

            async_write(socket_, boost::asio::buffer(message), [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    do_get_reply();
                }
            });
        }

        void do_get_reply(){
            auto self(shared_from_this());
            socket_.async_read_some(boost::asio::buffer(read_buffer, 8), [this, self](const boost::system::error_code ec, size_t length){
                if(!ec){
                    do_read();
                }else{
                    socket_.close();
                    return;
                }
            });
        }

        void do_read(){
            auto self(shared_from_this());
            bzero(read_buffer, max_length);
            socket_.async_read_some(boost::asio::buffer(read_buffer, max_length), [this, self](const boost::system::error_code ec, size_t length){
                if(!ec){
                    content = "";
                    content.assign(read_buffer);
                    bzero(read_buffer, max_length);
                    temp = do_replace(content);
                    cout << "<script>document.getElementById('s" << id_ << "').innerHTML += '" << temp << "';</script>" << endl;

                    if(temp.find("%")!= string::npos){
                        do_write();
                    }else{
                        do_read();
                    }
                }
            });
        }

        void do_write(){
            auto self(shared_from_this());
            cmd = "";
            getline(file, cmd);

            if(cmd.find("exit") != string::npos){
                file.close();
                host_info[id_].used = false;
            }
            cmd += "\n";
            temp = do_replace(cmd);
            
            cout << "<script>document.getElementById('s" << id_ << "').innerHTML += '<b>" << temp << "</b>';</script>" << endl;
            temp = "";

            async_write(socket_, boost::asio::buffer(cmd.c_str(), cmd.size()), [this, self](boost::system::error_code ec, size_t length){
                if(!ec){
                    if(host_info[id_].used == true) do_read();
                }
            });

        }

        int id_;
        ifstream file;
        boost::asio::ip::tcp::socket socket_{io_context};
        boost::asio::ip::tcp::resolver resolver_{io_context};
        string message = "";
        enum {max_length=10000};
        char read_buffer[max_length];
        string content = "";
        string cmd = "";
        string temp = "";
};

int main(){

    ParseQuery();
    int host_num = parameters.size()/3;
    InitConsole(host_num);

    for(int i=0; i<host_num; i++){
        string host = "h" + to_string(i);
        string port = "p" + to_string(i);
        string file = "f" + to_string(i);
        host_info[i].host = parameters[host];
        host_info[i].port = parameters[port];
        host_info[i].file = parameters[file];
        host_info[i].used = true;
    }

    for(int i=0; i<5; i++){
        if(host_info[i].used){
            make_shared<Client>(i)->start();
        }
    }
    io_context.run();


}



void InitConsole(int num){
    cout << "Content-type: text/html" << endl << endl;
    cout << "<!DOCTYPE html> " <<
            "<html lang=\"en\"> " <<
            "<head> " <<
            "<meta charset=\"UTF-8\" /> " <<
            "<title>NP Project 3 Console</title> " <<
            "<link " <<
                "rel=\"stylesheet\" " <<
                "href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\" " <<
                "integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\" " <<
                "crossorigin=\"anonymous\" " <<
            "/> " <<
            "<link " <<
                "href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\" " <<
                "rel=\"stylesheet\" " <<
            "/> " <<
            "<link " <<
                "rel=\"icon\" " <<
                "type=\"image/png\" " <<
                "href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\" " <<
            "/> " <<
            "<style> " <<
                "* { " <<
                    "font-family: 'Source Code Pro', monospace; " <<
                    "font-size: 1rem !important; " <<
                "} " <<
                "body { " <<
                    "background-color: #212529; " <<
                "} " <<
                "pre { " <<
                    "color: #cccccc; " <<
                "} " <<
                "b { " <<
                    "color: #01b468; " <<
                "} " <<
            "</style> " <<
            "</head> " <<
            "<body> " <<
            "<table class=\"table table-dark table-bordered\"> " <<
            "<thead> " <<
                "<tr> ";
    for(int i=0; i<num; i++){
        string host = "h" + to_string(i);
        string port = "p" + to_string(i);
        cout << "<th scope=\"col\"> " << parameters[host] << ":" << parameters[port] << "</th> ";
    }
        cout << "</tr> " <<
            "</thead> " <<
            "<tbody> " <<
            "<tr> ";
    for(int i=0; i<num; i++){
        cout << "<td><pre id=\"s" << i << "\" class=\"mb-0\"></pre></td> ";
    }
        cout << "</tr> " << "</tbody> " << "</table> " << 
        "</body> " << "</html>";
}

void ParseQuery(){
    //QUERY_STRING :[h0=nplinux11.cs.nctu.edu.tw&p0=1235&f0=t1.txt&h1=&p1=&f1=&h2=&p2=&f2=&h3=&p3=&f3=&h4=&p4=&f4=&sh=<sockshost>&sp=<socksport>]
    // host port file
    string query = getenv("QUERY_STRING");
    istringstream ss(query);

    string key_val, key, val;
    while(getline(ss, key_val, '&')){
        size_t found = key_val.find("=");
        if(found == key_val.length()-1) break;
        key = key_val.substr(0, found);
        val = key_val.substr(found+1);
        parameters[key] = val;
    }
    socks_host = parameters["sh"];
    socks_port = parameters["sp"];
}