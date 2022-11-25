#include "sha1.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <netdb.h>
#include <iostream>
#include <climits>
#include <unistd.h>
#include <arpa/inet.h>

#include <sstream>
#include <fstream>
#include <string>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <queue>
#include <string.h>

// #include <thread>  
#include <thread>
#include <mutex>

#include<ios>
#include<limits>

#define TRACKER_IP "127.0.0.1"
#define CHUNK_SIZE 524288

using namespace std;

struct peer{
    struct sockaddr_in address;
    vector<uint32_t> group_id;  //may remove, maybe 0 if not admin
};

struct file_info{
    string group_id;
    string file_path;
    string chunkmap;
};

unordered_map<string,file_info*> files;

struct chunk{
    uint32_t index;
    string sha1; //sha chunk
    file_info* owner;
    vector<peer> havers;
};

struct sockaddr_in server_address;
uint16_t currp;
uint16_t tracker_port = 8080;
mutex mtx;

// deque<file_info> started;
unordered_map<string, file_info*> ongoing;
unordered_map<string, file_info*> finished;

int inform_tracker(int request_code){    //pre-agreed messages to server
    int peer_fd;
    if ((peer_fd = socket(AF_INET, SOCK_STREAM , 0)) < 0){
        perror("Failed to create socket"); 
        exit(0);
    }

    if (connect(peer_fd,(struct sockaddr*) &server_address, sizeof(server_address)) < 0){
        perror("Failed to connect"); 
        exit(0);
    }
    //get files a client can download
    cout<<"Hitting "<<tracker_port<<endl;
    send(peer_fd, &request_code, sizeof(request_code), 0);
    return peer_fd;
}

long fileSize(string filename){
    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}

void handleRequest(int request_fd, string file_path, int chunk_index){
    FILE *f;
    f = fopen(&file_path[0], "rb");
    if (f == NULL) {
        perror("Could not read file");
    }
    char buf[524288];
    fseek(f, CHUNK_SIZE*chunk_index, SEEK_SET);
    
    if (fread(buf, 1, sizeof(buf), f)>0) {
        string hash = sha1((char *)(buf));
        cout<<"Hash for chunk "<<hash<<endl;
        int status=-1;
        struct timeval timeout;
        timeout.tv_sec = 60000;
        timeout.tv_usec = 0;
        setsockopt(request_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        send(request_fd, &hash[0], 40, 0);
        // memset(buf+strlen(buf), '\0', sizeof(buf)-strlen(buf));
        // for (int i=0;i<32;i++)
        send(request_fd, buf, CHUNK_SIZE, 0);
        int recv_size = recv(request_fd, &status, sizeof(status), 0);
        if (recv_size == -1){
            if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
                cout<<"Timeout for acknowledgement"<<endl;
        } else if (status==-1){ //send one more time
            cout<<"Could not send "<<endl;
            // send(request_fd, &hash, 20, 0);
            // send(request_fd, &buf, sizeof(buf), 0);
        }
        close(request_fd);
    } else
        ferror(f);
    fclose(f);

    // ifstream upload_file;
    // upload_file.open(file_path, ifstream::binary);
    // upload_file.seekg(CHUNK_SIZE*chunk_index, ios::beg);
    
    // int sub = 16384,i=0;
    // char buffer[sub];
    // while (!upload_file.eof() && i<32){
    //     upload_file.read(buffer, sub);

    // cout<<"File read for chunk "<<buffer[0]<<endl;
    // unsigned char *buf = (unsigned char *)buffer;

    // upload_file.close();
}

void send_chunkmap(int request_fd, string chunkmap){
    // chunkmap = to_string(num_chunks)+' '+chunkmap;
    send(request_fd, &chunkmap[0], strlen(&chunkmap[0]), 0);
}

void selfserver(){
    uint8_t request_fd,server_fd;
    if((server_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0){
        perror("Failed to create socket"); 
        exit(0);
    }

    struct sockaddr_in peer_2, peer_address;
    //initialize address map
    bzero(&peer_address, sizeof(peer_address));
    peer_address.sin_family = PF_INET; 
    peer_address.sin_addr.s_addr = INADDR_ANY; 
    
    peer_address.sin_port = htons(currp);

    //bind socket
    if(bind(server_fd, (struct sockaddr*) &peer_address, sizeof(peer_address)) < 0){
        perror("Failed to bind"); 
        exit(0);
    }

    //listen socket
    if(listen(server_fd, 5) < 0){  //max 5 pending connects
        perror("Failed to listen"); 
        exit(0);
    }
    
    while(1){
        int peer_2_length = sizeof(peer_2);
        if((request_fd = accept(server_fd,(struct sockaddr*) &peer_2, (socklen_t *) &peer_2_length)) < 0){
            perror("Failed to accept"); 
            exit(0);
        }
        else{
            char query[42];
            string hash;
            int chunk_index,query_type;
            read(request_fd, query, sizeof(query));
            // cout<<"Reading req "<<query<<endl;
            istringstream iss(query);
            iss>>query_type;
            char* h = (char *)malloc(41);
            strncpy(h,query+2,40);
            h[40] = '\0';
            hash = string(h);
            if (query_type==1){
                for (auto fl:files){
                    cout<<fl.first<<endl;
                }
                cout<<"Sending chunkmap for "<<files[hash]->file_path<<" "<<files[hash]->chunkmap<<endl;
                thread tid(&send_chunkmap, request_fd, files[hash]->chunkmap);
                tid.detach();
            } else if (query_type==2) {
                recv(request_fd, &chunk_index, sizeof(chunk_index), 0);
                
                // cout<<"give the file:" << files[hash]->file_path <<"," <<chunk_index<<endl;
                //process requests
                thread tid(&handleRequest, request_fd, files[hash]->file_path, chunk_index);
                tid.detach();
            } else {
                if (tracker_port==8080)
                    tracker_port = 8081;
                else
                    tracker_port = 8080;
                bzero(&server_address, sizeof(server_address));
                server_address.sin_family = PF_INET; 
                server_address.sin_addr.s_addr = inet_addr(TRACKER_IP); 
                server_address.sin_port = htons(tracker_port);
            }
        }
    }
}

void download_file_part(string file_path, string file_hash, int index, chunk chunk_piece, long file_length, bool toggle){
    string file_name = file_path.substr(file_path.find_last_of('/')+1);
    struct sockaddr_in peer_address;
    // int peer_index=0;
    // if (chunk_piece.havers.size()>1 && toggle)
    //     peer_index = 1;
    int peer_index = rand() % chunk_piece.havers.size();

    bzero(&peer_address, sizeof(peer_address));
    peer_address = chunk_piece.havers[peer_index].address;

    int sock_fd;
    //create socket
    if((sock_fd = socket(AF_INET, SOCK_STREAM , 0)) < 0){
        perror("Failed to create socket"); 
        exit(0);
    }

    //connect to server
    // cout<<"Probing "<<ntohs(peer_address.sin_port)<<endl;
    if(connect(sock_fd,(struct sockaddr*) &peer_address, sizeof(peer_address)) < 0){
        perror("Failed to connect"); 
        exit(0);
    }

    string q_file_hash = "2 "+file_hash;
    send(sock_fd, &q_file_hash[0], strlen(&q_file_hash[0]), 0);
    send(sock_fd, &index, sizeof(index), 0);
    char raw_hash[40];
    // read(sock_fd, raw_hash, sizeof(raw_hash));
    // istringstream iss(raw_hash);
    // iss>>query_type;
    read(sock_fd, raw_hash, 40);
    string h = (string)raw_hash;
    int chunk_size = 16384;
    char buffer[chunk_size];
    char full_chunk[CHUNK_SIZE];
    
    FILE *f;

    f = fopen(file_path.c_str(), "rb+");
    if (f == NULL) {
        perror("Could not read file");
        f = fopen(file_path.c_str(), "wb");
    }
    fseek(f, CHUNK_SIZE*index, SEEK_SET);
    string status="1";

    // recv(sock_fd, full_chunk, sizeof(full_chunk),0);
    // fwrite(full_chunk, 1, sizeof(full_chunk), f);
    if (index==(file_length/CHUNK_SIZE)){
        int i,rem = file_length%CHUNK_SIZE;
        for (i=0;i<rem/16384;i++){
            recv(sock_fd, buffer, sizeof(buffer),0);
            fwrite(buffer, 1, sizeof(buffer), f);
            memcpy(full_chunk+i*16384, buffer, sizeof(buffer));
        }
        recv(sock_fd, buffer, rem%sizeof(buffer),0);
        fwrite(buffer, 1, rem%sizeof(buffer), f);
        memcpy(full_chunk+i*16384, buffer, sizeof(buffer));
    }
    else{
        for (int i=0;i<32;i++){
            read(sock_fd, buffer, 16384);
            fwrite(buffer, 1, sizeof(buffer), f);
            memcpy(full_chunk+i*16384, buffer, sizeof(buffer));
            // memset()
            // cout<<"EF "<<strlen(buffer)<<endl;
        }
    }
    string hash = sha1(full_chunk);
    if (hash==h)
        cout<<"Chunk Verified"<<endl;
    fclose(f);
    send(sock_fd, &status[0], 1, 0);
    close(sock_fd);
    files[file_hash] = chunk_piece.owner;
}

void downloader(vector<chunk> chunks, file_info* new_file,string file_hash,int peer_fd,string user_id,string groupid,long file_length){
    vector<int> next_block;
    int rarest = 0,prev=-1;
    bool flag = false,corrupt=false, first_chunk=true;
    // int iter = 1;
    // cout<<"Starting"<<endl;

    
    do{
        next_block.clear();
        // cout<<"In iteration "<<iter++<<", downloading"<<endl;
        for(int i=0; i<4; i++){
            if (count(new_file->chunkmap.begin(),new_file->chunkmap.end(),'0') == next_block.size())
                break;
            
            // if ()
            // piece selection
            rarest = rand()%chunks.size();
            while (find(next_block.begin(),next_block.end(),rarest)!=next_block.end() || (new_file->chunkmap[rarest]=='1'))
                rarest = (rarest+1)%chunks.size();

            if (chunks[rarest].havers.size()==0){
                cout<<"Cannot download corrupted file, chunk "<<rarest<<" missing"<<endl;
                corrupt=true;
                break;
            }
            next_block.push_back(rarest);
            cout<<" "<<rarest;
        }
        cout<<endl;
        if (corrupt)
            break;
        flag=false;
        thread download_thread[4];
        bool toggle = false;
        for(unsigned int i=0; i<next_block.size(); i++){
            // sleep(1);
            // cout<<"Next "<<next_block[i]<<endl;
            download_thread[i] = thread(&download_file_part, new_file->file_path, file_hash, next_block[i], chunks[next_block[i]], file_length, toggle);
            toggle = !toggle;
            // pthread_detach(download_thread);
            // write_log(file_name, chunks);
        }
        for (unsigned int i=0; i<next_block.size(); i++){
            download_thread[i].join();
            new_file->chunkmap[next_block[i]] = '1';

            if (first_chunk){
                string inst; //sub_inst = "";
                peer_fd = inform_tracker(4);
                string file_name=new_file->file_path.substr(new_file->file_path.find_last_of('/')+1);
                inst = user_id+" "+groupid+" 2 "+file_name;
                // send(peer_fd, &port, sizeof(port), 0);
                // cout<<"Informing "<<inst<<endl;
                send(peer_fd, &inst[0], strlen(&inst[0]), 0);
                first_chunk = false;
            }
        }
        flag = true;
    } while(!next_block.empty());

    if (flag){
        // merge_chunks(new_file.file_path, chunks.size());
        ongoing.erase(file_hash);
        finished[file_hash] = new_file;
        cout<<"Written "<<new_file->file_path<<endl;
        
        FILE *f = fopen(&new_file->file_path[0], "rb");
        if (f == NULL) {
            perror("Could not read file");
        }
        char buf[524288];
        string computed_hash="";
        while (fread(buf, 1, sizeof(buf), f)>0) {
            computed_hash = computed_hash+sha1(buf);
        }
        computed_hash = sha1(computed_hash);
        fclose(f);
        if (file_hash==computed_hash)
            cout<<"File verified!"<<endl;
    }
}

void get_allargs(string rawargs, vector<string> &token){
    // Used to split string around spaces.
    string arg = "";
    bool qt=false;
    for (auto x : rawargs){
        if (x=='\'')
            qt = !qt;

        else if (x==' '){
            if (qt)
                arg += x;
            else{
                if (arg.length()>0)
                    token.push_back(arg);
                arg = "";
            }
        }
        else
            arg += x;
    }
    // if (qt)
    token.push_back(arg);
}

void term(){
    bool first_upload = false;
    int peer_fd,status=-1;
    string user_id="",command;
    // bool first_comm=true;
    while(1){
        // std::atomic<bool> flag = false;
        // thread([&]{
        //     this_thread::sleep_for(std::chrono::seconds(5));

        //     if (!flag)
        //         std::terminate();
        // }).detach();
        // cin.clear();
        cout<<"> ";
        // if (first_comm){
        //     cin.ignore(numeric_limits<streamsize>::max(),'\n');
        //     first_comm=false;
        // }
        command="";
        // bzero(&command[0],command.size());
        getline(cin, command);
        // flag = true;
        
        vector<string> parameters;
        vector<file_info> uploads;

        // parameters.clear();
        // istringstream iss(command);

        get_allargs(command,parameters);
        // cout<<parameters[0]<<endl;

        if (parameters[0] == "create_user"){
            peer_fd = inform_tracker(0);
            string credentials = parameters[1]+" "+parameters[2]+" "+to_string(currp);
            send(peer_fd, &credentials[0], strlen(&credentials[0]), 0);
            // send(peer_fd, &parameters[2], sizeof(parameters[2]), 0);
            char buffer[200];
            string data;
            int msglen = read(peer_fd, buffer, 200);
            // cout<<"In sock "<<buffer<<msglen<<endl;
            if (string(buffer)[0]=='1'){
                cout<<"Welcome to p2p with cpp"<<endl;
                user_id=parameters[1];
                status=1;
            } else
                cout<<string(buffer).substr(0,msglen)<<endl;
        }
        else if (parameters[0] == "login"){
            peer_fd = inform_tracker(2);
            string credentials = parameters[1]+" "+parameters[2]+" "+to_string(currp);
            send(peer_fd, &credentials[0], strlen(&credentials[0]), 0);
            char buffer[200];
            int msglen = read(peer_fd, buffer, 200);
            // cout<<"In sock "<<buffer<<msglen<<endl;
            if (string(buffer)[0]=='1' || string(buffer)[0]=='2'){
                user_id = parameters[1];
                if (string(buffer)[0]=='2')
                    cout<<"Already logged in!"<<endl;
                else
                    cout<<"Logged in!"<<endl;
                status = 1;
                // inform_tracker(14);
                // send(peer_fd, &parameters[1][0], strlen(&parameters[1][0]), 0);
                string upl = user_id+"_uploads.txt";
                FILE* f = fopen(upl.c_str(),"r");
                if (f != NULL){
                    char *line = NULL;
                    size_t len = 0;
                    ssize_t rd;
                    string file_hash,group_id,file_path,chunkmap;
                    
                    while ((rd = getline(&line, &len, f)) != -1) {
                        istringstream iss(line);
                        iss>>file_hash;
                        iss>>group_id;
                        iss>>file_path;
                        iss>>chunkmap;
                        files[file_hash] = new file_info();
                        files[file_hash]->group_id = group_id;
                        files[file_hash]->file_path = file_path;
                        files[file_hash]->chunkmap = chunkmap;
                    }

                    fclose(f);
                    if (line)
                        free(line);
                }
                upl = user_id+"_downloads.txt";
                FILE* f2 = fopen(upl.c_str(),"r");
                if (f2 != NULL){
                    char *line = NULL;
                    size_t len = 0;
                    ssize_t rd;
                    string work_type,group_id,file_path,chunkmap,filehash;
                    
                    while ((rd = getline(&line, &len, f2)) != -1) {
                        istringstream iss(line);
                        iss>>filehash;
                        iss>>work_type;
                        iss>>group_id;
                        iss>>file_path;
                        iss>>chunkmap;

                        file_info* fl = new file_info();
                        fl->group_id = group_id;
                        fl->file_path = file_path;
                        fl->chunkmap = chunkmap;
                        if (work_type=="C")
                            finished[filehash] = fl;
                        else
                            ongoing[filehash] = fl;
                    }

                    fclose(f2);
                    if (line)
                        free(line);
                }
                
                
            }
            else{
                cout<<string(buffer).substr(0,msglen)<<endl;
                status = -1;
            }
        }
        else if (parameters[0] == "logout"){
            peer_fd = inform_tracker(3);
            send(peer_fd, &user_id[0], strlen(&user_id[0]), 0);
            ofstream write(user_id+"_uploads.txt");
            string write_line,fn;
            for (auto i=files.cbegin();i!=files.cend();i++){
                // fn = i->second->file_path.substr(i->second->file_path.find_last_of('/')+1);
                write_line = i->first+" "+i->second->group_id+" "+i->second->file_path+" "+i->second->chunkmap;
                write<<write_line<<endl;
            }
            write.close();
            files.clear();
            ofstream down(user_id+"_downloads.txt");
            for (auto i=ongoing.cbegin();i!=ongoing.cend();i++){
                write_line = "D "+i->first+" "+i->second->group_id+" "+i->second->file_path+" "+i->second->chunkmap;
                down<<write_line<<endl;
            }
            for (auto i=finished.cbegin();i!=finished.cend();i++){
                write_line = "C "+i->first+" "+i->second->group_id+" "+i->second->file_path+" "+i->second->chunkmap;
                down<<write_line<<endl;
            }
            down.close();
            finished.clear();
            ongoing.clear();
            // recv(peer_fd, &status, sizeof(status), 0);
            user_id="";
            status = -1;
        }
        else if (parameters[0] == "create_group"){
            if (status==-1){
                cout<<"Login first!"<<endl;
                continue;
            }
            // send port info with is_admin flag of grp_id to tracker
            cout<<"creating..."<<endl;
            peer_fd = inform_tracker(1);
            string msg = user_id+' '+parameters[1];
            send(peer_fd, &msg[0], strlen(&msg[0]), 0);
            char raw_data[1000];
            read(peer_fd, raw_data, sizeof(raw_data));
            if (raw_data[0]=='1')
                cout<<"Created group successfully"<<endl;
            else
                cout<<"Not created"<<endl;
        }
        else if (parameters[0] == "join_group"){
            if (status==-1){
                cout<<"Login first!"<<endl;
                continue;
            }
            peer_fd = inform_tracker(8);
            string inst = user_id+' '+parameters[1];
            send(peer_fd, &inst[0], strlen(&inst[0]), 0);
            char raw_data[1000];
            read(peer_fd, raw_data, sizeof(raw_data));
            if (raw_data[0]=='1')
                cout<<"Sent request successfully"<<endl;
            else
                cout<<"Not requested"<<endl;
        }
        else if (parameters[0] == "leave_group"){
            if (status==-1){
                cout<<"Login first!"<<endl;
                continue;
            }
            peer_fd = inform_tracker(9);
            string inst = user_id+' '+parameters[1];
            send(peer_fd, &inst[0], strlen(&inst[0]), 0);
            cout<<"Left group successfully"<<endl;
        }
        else if (parameters[0] == "list_groups"){
            if (status==-1){
                cout<<"Login first!"<<endl;
                continue;
            }
            cout<<"Checking... "<<endl;
            char raw_data[10000];
            string msg,data;
            peer_fd = inform_tracker(12);
            string inst = "A";
            send(peer_fd, &inst[0], strlen(&inst[0]), 0);
            int msglen = read(peer_fd, raw_data, sizeof(raw_data));
            data = string(raw_data).substr(0,msglen);
            istringstream iss(data);
            int num_req;
            iss>>num_req;
            if (num_req==0)
                cout<<"No groups found!"<<endl;
            else{
                for(int i=0; i<num_req; i++){
                    iss>>msg;
                    cout<<msg;
                    if(i != num_req-1){
                        cout<<", ";
                    }
                }
                cout<<endl;
            }
        }
        else if (parameters[0] == "list_requests"){
            if (status==-1){
                cout<<"Login first!"<<endl;
                continue;
            }
            peer_fd = inform_tracker(11);
            string inst = user_id+" "+parameters[1];
            send(peer_fd, &inst[0], strlen(&inst[0]), 0);
            char raw_data[10000];
            int req_status;
            string msg,data;
            int msglen = read(peer_fd, raw_data, sizeof(raw_data));
            data = string(raw_data).substr(0,msglen);
            istringstream iss(data);
            iss>>req_status;
            if (req_status==0){
                iss>>msg;
                cout<<msg<<endl;
            }
            else {
                int num_req;
                iss>>num_req;
                for(int i=0; i<num_req; i++){
                    iss>>msg;
                    cout<<msg;
                    if(i != num_req-1){
                        cout<<", ";
                    }
                }
                cout<<endl;
            }
        }
        else if (parameters[0] == "accept_request"){
            if (status==-1){
                cout<<"Login first!"<<endl;
                continue;
            }
            peer_fd = inform_tracker(10);
            string inst = user_id+" "+parameters[1]+" "+parameters[2];
            send(peer_fd, &inst[0], strlen(&inst[0]), 0);
        }
        else if (parameters[0] == "list_files"){
            if (status==-1){
                cout<<"Login first!"<<endl;
                continue;
            }
            string file_name, req=parameters[1]+" "+user_id;
            peer_fd = inform_tracker(5);
            send(peer_fd, &req[0], strlen(&req[0]), 0);
            char raw_data[10000];
            int num_files;
            uint32_t file_length;
            
            read(peer_fd, raw_data, sizeof(raw_data));
            if (raw_data[0]=='0'){
                cout <<string(raw_data).substr(2,string(raw_data).length())<<endl;
                continue;
            }
            istringstream iss(raw_data);
            iss>>num_files;
            cout<<"From Group "<<parameters[1]<<": "<<endl;
            for(int i=0; i<num_files; i++){
                iss>>file_name;
                iss>>file_length;
                cout << file_name << ": "<< file_length <<" bytes";
                if(i != num_files-1){
                    cout<<", ";
                }
            }
            cout<<endl;
            close(peer_fd);
        }
        else if (parameters[0] == "upload_file"){
            if (status==-1){
                cout<<"Login first!"<<endl;
                continue;
            }
            string group_id=parameters[2];
            file_info* temp_file_info=new file_info();
            temp_file_info->group_id = group_id;
            temp_file_info->file_path = parameters[1];
            long file_length = fileSize(parameters[1]);
            int num_chunks = ceil(file_length/(float)CHUNK_SIZE);
            temp_file_info->chunkmap = string(num_chunks,'1');
            FILE *f;
            f = fopen(&temp_file_info->file_path[0], "rb");
            if (f == NULL) {
                perror("Could not read file");
            }
            // cout<<"File read successfully"<<endl;
            char buf[524288];
            string hash="";
            while (fread(buf, 1, sizeof(buf), f)>0) {
                hash = hash+sha1(buf);
            }
            hash = sha1(hash);
            fclose(f);
            cout<<"File hashed successfully"<<endl;
            string inst,sub_inst="";
            if (!first_upload){
                first_upload = true;
                inst = user_id+" "+group_id+" 0 ";
                sub_inst = " 127.0.0.1 "+to_string(currp);
                // thread user_thread(&selfserver);
                // user_thread.detach();
                // cout<<"Self server started"<<endl;
            }
            else
                inst = user_id+" "+group_id+" 1 ";
            peer_fd = inform_tracker(4);
            string file_name=parameters[1].substr(parameters[1].find_last_of('/')+1);
            inst+= file_name+" "+to_string(file_length)+" "+hash;
            cout<<"Sending "<<inst<<endl;
            files[hash] = temp_file_info;
            // send(peer_fd, &port, sizeof(port), 0);
            send(peer_fd, &inst[0], strlen(&inst[0]), 0);
        }
        else if (parameters[0] == "download_file"){
            if (status==-1){
                cout<<"Login first!"<<endl;
                continue;
            }
            file_info* new_file = new file_info();
            if (parameters[3][parameters[3].length()-1]!='/')
                new_file->file_path = parameters[3]+'/'+parameters[2];
            else
                new_file->file_path = parameters[3]+parameters[2];

            struct stat st;
            if (stat(parameters[3].c_str(), &st) == -1){
                cout<<"Destination path not valid!"<<endl;
                continue;
            }
            else{
                if ((S_ISDIR(st.st_mode)) && stat(new_file->file_path.c_str(), &st) != -1){
                    cout<<"Destination already has the file!"<<endl;
                    continue;
                }
            }
            string inst = parameters[1]+" "+parameters[2];
            
            new_file->group_id = parameters[1];
            // started.push_back(*new_file);
            peer_fd = inform_tracker(6);
            send(peer_fd, &inst[0], strlen(&inst[0]), 0);
            int number_of_results=0;
            char raw_data[10000];
            read(peer_fd, raw_data, sizeof(raw_data));
            // cout<<"REcv "<<raw_data<<endl;
            istringstream iss(raw_data);
            iss>>number_of_results;
            // cout<<"Search Responses "<<raw_data<<endl;
            // recv(peer_fd, &number_of_results, sizeof(number_of_results), 0);
            
            //file is not found
            if(number_of_results == 0){
                cout<<"File not found"<<endl;
                continue;
            }
            long file_length;
            //create data structure to store file location info
            // recv(peer_fd, &file_length, sizeof(file_length), 0);
            string file_hash;
            iss>>file_hash;
            iss>>file_length;
            int num_chunks = ceil(file_length/(float)CHUNK_SIZE);
            cout<<"Number of chunks = "<<num_chunks<<endl;
            new_file->chunkmap = string(num_chunks,'0');
            files[file_hash] = new_file;

            vector<chunk> chunks;
            for(int i = 0; i<num_chunks; i++){
                chunk temp_chunk;
                temp_chunk.index = i;
                temp_chunk.owner = new_file;
                chunks.push_back(temp_chunk);
            }
            
            peer temp_peer;
            string ip,chunk_index;
            char chunkmap[6000];
            uint16_t port;
            int sock_fd;
            for(int i=0; i<number_of_results; i++){
                iss>>ip;
                iss>>port;

                if (port>9999){
                    stringstream ss;
                    ss<<port;
                    string num = ss.str().substr(0,4);
                    istringstream ips(num);
                    ips>>port;
                }
                temp_peer.address.sin_addr.s_addr = inet_addr(&ip[0]);
                temp_peer.address.sin_family = AF_INET;
	            temp_peer.address.sin_port = htons(port);

                if((sock_fd = socket(AF_INET, SOCK_STREAM , 0)) < 0){
                    perror("Failed to create socket"); 
                    exit(0);
                }

                // cout<<"connect again "<<ip<<" "<<port<<ntohs(temp_peer.address.sin_port)<<endl;

                //connect to server
                if(connect(sock_fd,(struct sockaddr*) &temp_peer.address, sizeof(temp_peer.address)) < 0){
                    perror("Failed to connect"); 
                    exit(0);
                }
                string q_file_hash = "1 "+file_hash;

                cout<<"sending "<<q_file_hash<<" = "<<strlen(&q_file_hash[0])<<endl;
                send(sock_fd, &q_file_hash[0], strlen(&q_file_hash[0]), 0);
    
                read(sock_fd, chunkmap, sizeof(chunkmap));
                chunk_index = string(chunkmap);
                if (chunk_index.find('1')==string::npos){
                    cout<<"No chunks found in "<<port<<endl;
                    continue;
                }

                // cout<<"Received map-> "<<chunk_index<<endl;
                close(sock_fd);

                for(int k = 0; k<num_chunks; k++){
                    if(chunk_index[k]=='1'){
                        chunks[k].havers.push_back(temp_peer);
                    }
                }
            }

            // started.pop_back();
            ongoing[file_hash] = new_file; 
            thread dthd(&downloader,chunks,new_file,file_hash,peer_fd,user_id,parameters[1],file_length);
            dthd.detach();
        }
        else if (parameters[0] == "show_downloads"){
            if (status==-1){
                cout<<"Login first!"<<endl;
                continue;
            }
            for (auto i=ongoing.cbegin();i!=ongoing.cend();i++)
                cout<<"[D] ["<<i->second->group_id<<"] "<<i->second->file_path<<";"<<i->second->chunkmap<<endl;
            for (auto i=finished.cbegin();i!=finished.cend();i++)
                cout<<"[C] ["<<i->second->group_id<<"] "<<i->second->file_path<<endl;
        }
        else if (parameters[0] == "show_shares"){
            for (auto fv:files){
                cout<<fv.first<<" = "<<fv.second->file_path<<endl;
            }
        }
        else if (parameters[0] == "stop_share"){
            inform_tracker(14);
            string inst = user_id+" "+parameters[1]+" "+parameters[2];
            unordered_map<string, file_info*>::iterator fit = files.begin();
            while (fit != files.end()) {
                if (fit->second->group_id==parameters[1] && fit->second->file_path.find(parameters[2]) != string::npos)
                    fit = files.erase(fit);
                else
                    fit++;
            }
            send(peer_fd, &inst[0], strlen(&inst[0]), 0);

        }
        else{
            cout<<"command not found"<<endl;
        }
        // cin.ignore(numeric_limits<streamsize>::max(),'\n');
        // sleep(20);
        // break;
    }
    
    
    return;
}

int main(int argc, char** argv){  //main func
    
    // cin>>currp;
    // self_addr = argv[1]
    stringstream ss(argv[1]);
    string add;
    getline(ss, add, ':');
    ss>>currp;


    // currp = stoi(argv[1]);


    //initialize server
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = PF_INET; 
    server_address.sin_addr.s_addr = inet_addr(TRACKER_IP);  //same as tracker ip
    server_address.sin_port = htons(tracker_port); 
    thread user_thread(&selfserver);
    user_thread.detach();
    term();
    
    return 0;

}