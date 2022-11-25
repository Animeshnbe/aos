#include <netinet/in.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <sstream>
#include <fstream>
#include <arpa/inet.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#define PORT 8080
#define PORT2 8081
using namespace std;

struct req{
    int new_sock;
    uint32_t request_type;
    struct sockaddr_in client_address;
    req(int s,uint32_t typ, struct sockaddr_in ca){
        new_sock=s;
        request_type=typ;
        client_address=ca;
    }
};

struct user{
    struct sockaddr_in peer_address;
    string userid;
    string pwd;
    uint16_t listen_port;
    string listen_ip;
    bool logged;
    user(sockaddr_in add, string uid, string p, uint16_t lp, string li, bool is_li){
        peer_address = add;
        userid = uid;
        pwd = p;
        listen_port = lp;
        listen_ip = li;
        logged = is_li;
    }
};

struct file_info{
    string file_name;
    string sha1hash;
    long file_length;
    unordered_set<user*> havers;
    file_info(string fn, string hash, long len){
        file_name = fn;
        sha1hash = hash;
        file_length = len;
    }
};

struct group{
    string admin;
    vector<string> pending;
    vector<string> members;
    unordered_map<string,file_info*> shared;
};

unordered_map<string,user*> users;
unordered_map<string,group> groups; //group_id, rest of data
// vector<file_info> files;

void* handle_request(void *input) {
    int request_type = ((struct req*)input)->request_type;
    int new_sock = ((struct req*)input)->new_sock;
    cout<<"Hello "<<request_type<<" "<<((struct req*)input)->client_address.sin_port<<" "<<((struct req*)input)->new_sock<<endl;

    char raw_info[100000];
    string user_id,pwd,group_id,admin_id;
    string file_name,file_sha;
    short first_upload;
    string status,data;
    uint16_t port;
    int msglen = read(new_sock, raw_info, sizeof(raw_info));
    data = string(raw_info).substr(0,msglen);
    istringstream iss(data);

    switch (request_type){
        case 0:{
            getline( iss, user_id, ' ');
            getline( iss, pwd, ' ');
            iss>>port;
            cout<<"Got port "<<port<<endl;
            if (users.find(user_id)!=users.end()){
                status = "0 User already signed up";
                send(new_sock, &status[0], strlen(&status[0]), 0);
                break;
            }
            users[user_id] = new user(((struct req*)input)->client_address,user_id,pwd,port,"127.0.0.1",true);
            status = "1";
            send(new_sock, &status[0], strlen(&status[0]), 0);
            cout<<"Success "<<user_id<<endl;
            break;
        }
        case 1:{
            getline( iss, user_id, ' ' );
            getline( iss, group_id, ' ' );
            string req;
            if (groups.find(raw_info)!=groups.end()){
                req = "0 Group already exists";
                send(new_sock, &req[0], strlen(&req[0]), 0);
                break;
            }
            group g;
            if (users.find(user_id)==users.end())
                req = "0 Invalid request from non-existent user";
            else {
                g.admin = user_id;
                g.members.push_back(user_id);
                groups[group_id] = g;
                req = "1";
            }
            send(new_sock, &req[0], strlen(&req[0]), 0);
            break;
        }
        case 2:{
            getline( iss, user_id, ' ');
            getline( iss, pwd, ' ');
            iss>>port;
            if (users.find(user_id)==users.end()){
                // cout<<"User does not exist"<<endl;
                status = "User does not exist";
                send(new_sock, &status[0], strlen(&status[0]), 0);
                break;
            }
            else if (users[user_id]->pwd!=pwd){
                cout<<"Incorrect password "<<users[user_id]->pwd<<", "<<pwd<<endl;
                status = "Incorrect password";
                send(new_sock, &status[0], strlen(&status[0]), 0);
                break;
            }
            else if (users[user_id]->logged){
                status = "2";
                send(new_sock, &status[0], strlen(&status[0]), 0);
                break;
            }
            users[user_id]->peer_address = ((struct req*)input)->client_address;
            users[user_id]->logged = true;
            users[user_id]->listen_port = port;
            status = "1";
            cout<<"New login"<<endl;
            send(new_sock, &status[0], strlen(&status[0]), 0);
            break;
        }
        case 3:{
            if (users.find(raw_info)==users.end()){
                cout<<"User does not exist"<<endl;
                break;
            }
            cout<<"Logging out"<<endl;
            user_id = "";
            users[raw_info]->logged = false;
            break;
        }
        case 4:{
            iss>>user_id;
            iss>>group_id;
            cout<<group_id<<endl;
            if (groups.find(group_id)==groups.end()){
                cout<<"Group does not exist"<<endl;
                break;
            }
            iss>>first_upload;
            iss>>file_name;
            struct file_info *f;
            long file_length;
            cout<<"Upload requested by "<<user_id<<endl;
            // 0 whole first time, 1 whole next time, 2 chunk ft, 3 chunk nt
            if (first_upload==0 || first_upload==1){
                iss>>file_length;
                iss>>file_sha;
                f = new file_info(file_name, file_sha, file_length);
                if (groups[group_id].shared.find(file_name)==groups[group_id].shared.end())
                    groups[group_id].shared[file_name] = f;
                else{
                    if (groups[group_id].shared[file_name]->sha1hash!=f->sha1hash){
                        cout<<"Cannot upload same filename with different SHA in same group!"<<endl;
                        break;
                    }
                    f = groups[group_id].shared[file_name];
                }
                
            }
            else {
                f = groups[group_id].shared[file_name];
                
            }
            f->havers.insert(users[user_id]);

            auto fir = *(f->havers.begin());
            cout<<"Updated ports "<<fir->listen_port<<endl;
            cout<<"Uploaded successfully "<<groups[group_id].shared[file_name]->file_length<<", "<<file_sha<<endl;
            break;
        }
        case 5:{
            string req="",group_id;
            int count = 0;
            iss>>group_id;
            iss>>user_id;
            // cout<<"Got "<<group_id<<endl;
            if (groups.find(group_id)==groups.end()){
                req = "0 Group does not exist "+string(group_id);
                send(new_sock, &req[0], strlen(&req[0]), 0);
                break;
            }
            if (find(groups[group_id].members.begin(),groups[group_id].members.end(),user_id)==groups[group_id].members.end()){
                req = "0 You aren't part of this group: "+string(group_id);
                send(new_sock, &req[0], strlen(&req[0]), 0);
                break;
            }
            for (auto f: groups[group_id].shared){
                count++;
                req+=(' '+f.first);
                cout<<"Length "<<f.second->file_length<<endl;
                cout<<"sha1 "<<f.second->sha1hash<<endl;
                cout<<"Haver ";
                for (auto hv:f.second->havers)
                    cout<<hv->userid<<" "<<hv->listen_port<<endl;
                req+=(' '+to_string(f.second->file_length));
            }
            req = to_string(count)+req;
            send(new_sock, &req[0], strlen(&req[0]), 0);
            break;
        }
        case 6:{
            getline( iss, group_id, ' ' );
            getline( iss, file_name, ' ' );
            string req="";
            if (groups.find(group_id)==groups.end()){
                req = "Group does not exist";
                send(new_sock, &req[0], strlen(&req[0]), 0);
                cout<<"Group does not exist "<<group_id<<":"<<file_name<<endl;
                break;
            }
            cout<<"Given "<<group_id<<":"<<file_name<<endl;
            if (groups[group_id].shared.find(file_name)==groups[group_id].shared.end()){
                cout<<"File does not exist in this group "<<file_name<<endl;
                req = "File does not exist in this group";
                send(new_sock, &req[0], strlen(&req[0]), 0);
                break;
            }
            string sha1=groups[group_id].shared[file_name]->sha1hash;
            int count=0;
            auto fir = *(groups[group_id].shared[file_name]->havers.begin());
            cout<<"Sha1 -> "<<sha1<<" "<<fir->listen_port<<endl;
            for (auto f:groups[group_id].shared[file_name]->havers){
                if (f->logged){
                    count++;
                    req += (' '+f->listen_ip);
                    req += (' '+to_string(f->listen_port));
                }
            }

            req = to_string(count)+" "+sha1+" "+to_string(groups[group_id].shared[file_name]->file_length)+req;
            send(new_sock, &req[0], strlen(&req[0]), 0);
            break;
        }
        case 7:
            cout<<"Meow"<<endl;
            break;
        case 8:{
            getline( iss, user_id, ' ' );
            getline( iss, group_id, ' ' );
            string req;
            if (groups.find(group_id)==groups.end()){
                req = "0 Group does not exist";
                send(new_sock, &req[0], strlen(&req[0]), 0);
            }
            else if (find(groups[group_id].members.begin(),groups[group_id].members.end(),user_id)!=groups[group_id].members.end()){
                req = "0 Already part of group";
                send(new_sock, &req[0], strlen(&req[0]), 0);
            }
            else {
                groups[group_id].pending.push_back(user_id);
                req = "1";
                send(new_sock, &req[0], strlen(&req[0]), 0);
            }
            break;
        }
        case 9:{
            getline( iss, user_id, ' ' );
            getline( iss, group_id, ' ' );
            if (groups.find(group_id)==groups.end()){
                cout<<"Group does not exist"<<endl;
                break;
            }
            if (groups[group_id].members.size()==1){
                cout<<"Removing the group "<<group_id<<endl;
                groups.erase(group_id);
                break;
            }
            unordered_map<string, file_info*>::iterator fit = groups[group_id].shared.begin();
            while (fit != groups[group_id].shared.end()) {
                auto fir = *(fit->second->havers.begin());
                if (fit->second->havers.size()==1 && fir->userid==user_id)
                    fit = groups[group_id].shared.erase(fit);
                else
                    fit++;
            }
            groups[group_id].members.erase(find(groups[group_id].members.begin(),groups[group_id].members.end(),user_id));
            if (groups[group_id].admin==user_id)
                groups[group_id].admin = groups[group_id].members[0]; //next becomes new admin
            break;
        }
        case 10:{
            getline( iss, admin_id, ' ' );
            getline( iss, group_id, ' ' );
            getline( iss, user_id, ' ' );
            cout<<"got "<<admin_id<<":"<<group_id<<":"<<user_id<<endl;
            if (groups.find(group_id)==groups.end()){
                cout<<"Group does not exist"<<endl;
                break;
            }
            
            if (groups[group_id].admin == admin_id && find(groups[group_id].pending.begin(),groups[group_id].pending.end(),user_id)!=groups[group_id].pending.end()){
                groups[group_id].members.push_back(user_id);
                groups[group_id].pending.erase(find(groups[group_id].pending.begin(),groups[group_id].pending.end(),user_id));
                cout<<"New member added!"<<endl;
            }
            else
                cout<<"Permission denied!"<<endl;
            break;
        }
        case 11:{
            getline( iss, admin_id, ' ' );
            getline( iss, group_id, ' ' );
            cout<<"got "<<admin_id<<":"<<group_id<<endl;
            string req;
            if (groups.find(group_id)==groups.end()){
                req = "0 Group does not exist";
                send(new_sock, &req[0], strlen(&req[0]), 0);
            }
            else if (groups[group_id].admin == admin_id){
                int count = 0;
                req="";
                for (auto& i:groups[group_id].pending){
                    count++;
                    req+=(' '+i);
                }
                req = "1 "+to_string(count)+req;
                send(new_sock, &req[0], strlen(&req[0]), 0);
            }
            else{
                req = "0 Permission denied!";
                send(new_sock, &req[0], strlen(&req[0]), 0);
            }
            break;
        }
        case 12:{
            string req="";
            int count=0;
            for (auto i:groups){
                count++;
                req+=(' '+i.first);
            }
            cout<<"Found groups "<<req<<endl;
            req = to_string(count)+req;
            send(new_sock, &req[0], strlen(&req[0]), 0);
            break;
        }
        case 13:
            getline( iss, file_name, ' ' );
            getline( iss, group_id, ' ' );
            getline( iss, user_id, ' ' );
            if (groups.find(group_id)==groups.end()){
                cout<<"Group does not exist"<<endl;
                break;
            }
            if (groups[group_id].shared.find(file_name)==groups[group_id].shared.end())
                cout<<"This file is not in this group"<<endl;
            else if (groups[group_id].admin == user_id || find(groups[group_id].members.begin(),groups[group_id].members.end(),user_id)!=groups[group_id].members.end()){
                groups[group_id].shared.erase(file_name);
                cout<<"File removed from the group"<<endl;
            } else 
                cout<<"Permission denied!"<<endl;
            break;
        case 14:
            iss>>user_id;
            iss>>group_id;
            iss>>file_name;
            if (groups.find(group_id)==groups.end()){
                cout<<"Group does not exist"<<endl;
                break;
            }
            if (groups[group_id].shared.find(file_name)==groups[group_id].shared.end()){
                cout<<"File no longer part of this group"<<group_id<<endl;
                break;
            }
            groups[group_id].shared[file_name]->havers.erase(users[user_id]);
            if (groups[group_id].shared[file_name]->havers.size()==0){
                cout<<"File deleted from the group"<<endl;
                groups[group_id].shared.erase(file_name);
            }
            else {
                for (auto hv:groups[group_id].shared[file_name]->havers)
                    cout<<hv->userid<<" "<<hv->listen_port<<endl;
            }
        default:
            break;
    }
    close(new_sock);
    return 0;
}

void switch_server(){
    int sock = 0, client_fd;
	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;

    for (auto user:users){
        serv_addr.sin_port = htons(user.second->listen_port);
        char hello[43] = "3 xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            cout<<"\n Socket creation error \n";
            return;
        }

        if (inet_pton(AF_INET, (&user.second->listen_ip[0]), &serv_addr.sin_addr) <= 0) {
            cout<<"\nInvalid address/ Address not supported \n";
            return;
        }

        if ((client_fd = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0) {
            printf("\nConnection Failed \n");
            return;
        }
        
        send(sock, hello, sizeof(hello), 0);
    }
}

void* tracker2(void *input) {
    int new_peer, server_fd;
    struct sockaddr_in server_address, client_address;
    //create socket
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Failed to create socket"); 
        exit(0);
    }

    //initailize address structure
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = PF_INET; 
    server_address.sin_addr.s_addr = INADDR_ANY; 
    server_address.sin_port = htons(PORT2); 

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET,SO_REUSEADDR | SO_REUSEPORT, &opt,sizeof(opt))) {
        perror("setsockopt");
        exit(0);
    }

    //bind socket
    if(bind(server_fd, (struct sockaddr*) &server_address, sizeof(server_address)) < 0){
        perror("Failed to bind"); 
        exit(0);
    }

    //listen socket
    if(listen(server_fd, 5) < 0){
        perror("Failed to listen"); 
        exit(0);
    }

    cout<<"listening in new tracker..."<<endl;
    while(1){
        int client_address_length = sizeof(client_address);
        if((new_peer = accept(server_fd,(struct sockaddr*) &client_address, (socklen_t *) &client_address_length)) < 0){
            perror("Failed to accept"); 
            exit(0);
        }
        // if 
        uint32_t request_type;
        recv(new_peer, &request_type, sizeof(request_type), 0);
        
        
        pthread_t thd;
        req* input = new req(new_peer, request_type, client_address);
        pthread_create(&thd, NULL, handle_request, (void *)input);
        // pthread_detach(thd);
        char* b;
        pthread_join(thd,(void**)&b);
        free(b);
        switch_server();
    }
}

int main(int argc, char const* argv[]){
    // server tracker;
    int new_peer, server_fd;
    pthread_t alt;
    pthread_create(&alt, NULL, tracker2, NULL);
    pthread_detach(alt);

    struct sockaddr_in server_address, client_address;
    //create socket
    if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("Failed to create socket"); 
        exit(0);
    }

    bzero(&server_address, sizeof(server_address));

    if (argc>1){
        ifstream tracker_info(argv[1]);
        istringstream iss(argv[2]);
        int linenum;
        iss>>linenum;
        tracker_info.seekg(linenum);
        string tracker_add;
        getline(tracker_info, tracker_add);
        istringstream iss2(tracker_add);
        string ip;
        uint16_t port;
        iss2>>ip;
        iss2>>port;
        // inet_pton(AF_INET, "192.0.2.33", &(server_address.sin_addr));
    } else {

    //initailize address structure
    
        server_address.sin_family = PF_INET; 
        server_address.sin_addr.s_addr = INADDR_ANY; 
        server_address.sin_port = htons(PORT); 
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET,SO_REUSEADDR | SO_REUSEPORT, &opt,sizeof(opt))) {
        perror("setsockopt");
        exit(0);
    }

    //bind socket
    if(bind(server_fd, (struct sockaddr*) &server_address, sizeof(server_address)) < 0){
        perror("Failed to bind"); 
        exit(0);
    }

    //listen socket
    if(listen(server_fd, 5) < 0){
        perror("Failed to listen"); 
        exit(0);
    }

    cout<<"listening..."<<endl;
    while(1){
        int client_address_length = sizeof(client_address);
        if((new_peer = accept(server_fd,(struct sockaddr*) &client_address, (socklen_t *) &client_address_length)) < 0){
            perror("Failed to accept"); 
            exit(0);
        }
        uint32_t request_type;
        recv(new_peer, &request_type, sizeof(request_type), 0);
        
        pthread_t thd;
        char* b;
        req* input = new req(new_peer, request_type, client_address);
        pthread_create(&thd, NULL, handle_request, (void *)input);
        pthread_join(thd,(void**)&b);
        free(b);
        switch_server();
    }
    return 0;

    // int server_fd, valread;

    // char buffer[1024] = { 0 };
    // valread = read(new_peer, buffer, 1024);
    // cout<<buffer<<endl;
    // send(new_peer, &hello[0], strlen(&hello[0]), 0);
 
    // close(new_peer);
    // shutdown(server_fd, SHUT_RDWR);
}

