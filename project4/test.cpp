#include <iostream>
#include <string>
#include <sstream>

using namespace std;

int main(){
    string text = "permit b 140.113.*.*";
    stringstream s(text);
    string event, type, ip;
    s >> event >> type >> ip;

    cout << event << "," << type << "," << ip << endl; 
}