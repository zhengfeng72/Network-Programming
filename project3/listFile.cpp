#include <string>
#include <iostream>
#include <filesystem>
#include <dirent.h>
#include <vector>
#include <algorithm>
using namespace std;

int main(){
    // string path = "./bin";
    // for(const auto &entry : std::filesystem::directory_iterator(path))
    //     cout << entry.path() << endl;

    DIR *dr;
    struct dirent *en;
    dr = opendir("./test_case");
    string file;
    vector<string> files;
    if(dr){
        while((en = readdir(dr))!=NULL){
            if(en->d_type==DT_REG){
                file="";
                file.assign(en->d_name);
                cout << file << endl;
                files.push_back(file);
            }
                // printf("%hhd %s\n",en->d_type, en->d_name);
        }
    }
    closedir(dr);

    sort(files.begin(), files.end());

    for(int i=0; i<files.size(); i++){
        cout << i << " " << files[i] << endl;
    }
}