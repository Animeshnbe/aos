#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <unistd.h>
#include <cstdio>
#include <iostream>
#include <fcntl.h>
#include <stack>
#include <ctime>
#include <pwd.h>
#include <grp.h>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <limits.h>

using namespace std;
#define moveCursor(x, y) std::cout<<"\033["<<(x)<<";"<<(y)<<"H";
#define clrscr() std::cout<<"\033[H\033[J";
#define CTRL_KEY(c) ((c) & 0x1f)

struct winsize term;

//error handling
void die(const char *s) {
    perror(s);
    exit(1);
}

struct pctrl
{
    string homePath;
    stack<string> visited,next;
    int count=0;
    int linenum=0;
    int cursorptr;
    string currentDir;
    vector<string> comm;
    bool foundFile=false;
} controller;

struct filer
{
    string name;
    string size;
    string perm;
    string links;
    string og;
    string ts;
};

vector<filer> all;

struct termios orig_termios,original;

void disableRaw(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void enableRaw(bool ech=false){
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRaw);

    struct termios raw = orig_termios;

    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    if (ech)
        raw.c_lflag &= ~(ICANON);
    else
        raw.c_lflag &= ~(ECHO | ICANON);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


string permissions(char *file){
    struct stat st;
    string modeval;
    stat(file, &st);
    if(stat(file, &st) == 0){
        mode_t perm = st.st_mode;
        if( perm & S_IFDIR )
            modeval += 'd';
        else if( perm & S_IFREG )
            modeval += 'f';
        else
            modeval += '-';
        modeval += (perm & S_IRUSR) ? 'r' : '-';
        modeval+= (perm & S_IWUSR) ? 'w' : '-';
        modeval+= (perm & S_IXUSR) ? 'x' : '-';
        modeval+= (perm & S_IRGRP) ? 'r' : '-';
        modeval+= (perm & S_IWGRP) ? 'w' : '-';
        modeval+= (perm & S_IXGRP) ? 'x' : '-';
        modeval+= (perm & S_IROTH) ? 'r' : '-';
        modeval+= (perm & S_IWOTH) ? 'w' : '-';
        modeval+= (perm & S_IXOTH) ? 'x' : '-';
        
    }
    else{
        modeval= "----------";
    }   
    return modeval;
}

string links(char* file){
    struct stat st;
    stat(file, &st);
    if(stat(file, &st) == 0)
        return to_string(st.st_nlink)+' ';
    else
        return "0 ";
}

string ownergroup(char* file){
    string modeval;
    struct stat st;
    stat(file, &st);
    if(stat(file, &st) == 0){
        struct passwd *owner = getpwuid(st.st_uid);
        struct group  *group = getgrgid(st.st_gid);
        
        if (owner!=0){
            modeval+=' ';
            modeval+=owner->pw_name;
        }
        if (group!=0){
            modeval+=' ';
            modeval+=group->gr_name;
        }
        else {
            modeval+=" \t ";
        }
    }
    else {
        modeval="";
    }
    return modeval;
}

string get_path(string token)
{
    if (token[0] == '/'){
        if (token[token.length() - 1]=='/') //remove slash at end
            return token.substr(0,token.length() - 1);
        return token;
    }

    string currentdirpath = controller.homePath;

    if (token==".")
        return currentdirpath;

    if (token[0] == '.' && token[1] != '\0'){
        if (token[1]=='.'){ //up one level only
            string parent = currentdirpath.substr(0,currentdirpath.find_last_of('/'));
            return parent + token.substr(2, token.length() - 1);
        }
        else
            return currentdirpath + "/" + token.substr(2, token.length() - 1);
    }

    // else if(token[0] == '/')
    // return currentdirpath + "/" + token.substr(2, token.length()-1);

    else if (token[0] == '~' && token[1] != '\0'){
        char cwd[PATH_MAX];
        return getcwd(cwd, PATH_MAX) + token.substr(2, token.length() - 1);
    }

    else if (token[0] == '~'){
        char cwd[PATH_MAX];
        return getcwd(cwd, PATH_MAX);
    }

    else
        return currentdirpath+"/"+token;
}

string readableSize(size_t size) {              
    static const char *SIZES[] = { "B", "K", "M", "G", "T" };
    int res = 0;
    size_t rem = 0;

    while (size >= 1024 && res < (sizeof SIZES/sizeof *SIZES)) {
        rem = (size%1024);
        res++;
        size /= 1024;
    }

    double size_d = (float)size+(float)rem/1024.0; //exact size
    int d = (int)(size_d*100+.5);
    string size_format = to_string(d/100);
    string result = size_format+SIZES[res];
    return result;
}

bool initialise(const char *dirname)
{
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &term); // initalize value of w based on screen

    struct dirent *dent;
    DIR *dir = opendir(dirname);
  
    if (dir == NULL){
        cout<<"Could not open this directory";
        return false;
    }
    controller.cursorptr = 1;
    controller.homePath = dirname;

    vector<string> files;
    string ts_act;
    while ((dent = readdir(dir)) != NULL){
        
        files.push_back(dent->d_name);
    }
    sort(files.begin(),files.end());

    controller.count=0;
    all.clear();
    for (int i=0;i<files.size();i++){
        string s(files[i]);
        string sdir(dirname);
        s = sdir + '/' + s;
        char *nm = &s[0];

        struct stat file_stats,ts;
        if (stat(nm, &file_stats) == -1)
            perror(nm);

        else{
            controller.count++;
            ts_act = ctime(&file_stats.st_mtime);
            struct filer fs;
            fs.perm = permissions(nm);
            fs.og = ownergroup(nm);
            fs.links = links(nm);
            fs.size = readableSize(file_stats.st_size);
            fs.ts = ts_act.substr(4,12);
            fs.name = files[i];
            all.push_back(fs);
        }
    }
    closedir(dir);
    return true;
}


void explorer(int pos, int tot){
    clrscr();
    
    if (controller.linenum<=0)
        controller.linenum=0;
    cout<<controller.homePath<<", "<<controller.count<<", "<<tot<<endl;
    // if (controller.visited.empty())
    //     cout<<endl;
    // else
    //     cout<<controller.visited.top()<<endl;
    int showlines=controller.count;
    if (controller.count>tot-4){
        //vertical overflow
        showlines=tot-4;
    }

    for (int i=0;i<showlines && pos+i<all.size();i++){
        if (i==controller.linenum)
            cout<<">>>  "<<all[pos+i].perm<<" "<<setw(3)<<right<< all[pos+i].links<<all[pos+i].og<< setw(5) << right<<all[pos+i].size<<" "<<all[pos+i].ts<<" "<<all[pos+i].name<<endl;
        else
            cout<<"     "<<all[pos+i].perm<<" "<<setw(3)<<right<< all[pos+i].links<<all[pos+i].og<< setw(5) << right<<all[pos+i].size<<" "<<all[pos+i].ts<<" "<<all[pos+i].name<<endl;
    }

    moveCursor(tot-1, 0);
    cout << "Normal Mode, Press : to switch to command mode\n";
    
}

void pathsearch(char* dirname, string name){
    struct dirent *dent;
    DIR *dir = opendir(dirname);
  
    if (dir == NULL){
        cout<<"No results found";
        return;
    }

    string ts_act;
    while ((dent = readdir(dir)) != NULL){
        if (dent->d_name==name){
            // if (strcmp(dirname,".")==0)
            //     cout<<"Results in current directory ";
            // else
            //     cout<<"Results in "<<dirname<<" ";
            cout<<"True";
            controller.foundFile=true;
            return;
        }
        if (dent->d_type==DT_DIR && strcmp(dent->d_name,".")!=0 && strcmp(dent->d_name,"..")!=0){
            char path[100] = {0};
            strcat(path,dirname);
            strcat(path,"/");
            strcat(path,dent->d_name);
            pathsearch(path,name);
            if (controller.foundFile)
                break;
        }
    }
}

void uplvl(){

}

void back(){

}

void forward(){

}

void create_file(string& name,string dest){
    fstream file;
    if (dest==".")
        file.open(controller.homePath+"/"+name,ios::out);
    else
        file.open(get_path(dest)+"/"+name,ios::out);
    if(!file){
        fstream file(name, fstream::in | fstream::out | fstream::trunc);
    }
}

int create_dir(string& dirname,string dest){
    dest = get_path(dest);
    struct stat st;
    if (stat(dest.c_str(), &st) == -1){
        cout<<"Destination not found";
        return -1;
    }
    if (S_ISDIR(st.st_mode)){
        dest+="/"+dirname;
        return mkdir(dest.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }
    cout<<"MKDIR Failed";
    return -1;
}

void copy_file(string src, string dst)
{
    char writeBlock[1024];
    struct stat sourceInfo, destnInfo;
    int r;

    int outdata = open(dst.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    int data = open(src.c_str(), O_RDONLY);
    
    if (stat(src.c_str(), &sourceInfo) == -1){
        cout<<"Cannot open file";
        return;
    }
    if (stat(dst.c_str(), &destnInfo) == -1){
        cout<<"Destination directory not found\n";
        return;
    }

    while (r = read(data, writeBlock, sizeof(writeBlock)) > 0)
        write(outdata, writeBlock, r);
    chown(dst.c_str(), sourceInfo.st_uid, sourceInfo.st_gid);
    chmod(dst.c_str(), sourceInfo.st_mode);
}

void copy_folder(string src, string dest){
    DIR *d;
    struct dirent *dir;
    int ret = mkdir(dest.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); //make if not present
    d = opendir(src.c_str());
    if (d == NULL){
        cout<<"Cannot open source\n";
        return;
    }
    while ((dir = readdir(d)) != NULL){
        if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
            ;
        else
        {
            // string name = dir->d_name;
            string eachfile = src + "/" + dir->d_name;
            struct stat st;
            if (stat(eachfile.c_str(), &st) == -1)
            {
                cout << "error 2";
                return;
            }

            if (S_ISDIR(st.st_mode))
                copy_folder(eachfile, dest + "/" + dir->d_name);
            else
                copy_file(eachfile, dest + "/" + dir->d_name);
        }
    }
}

void my_rename(string& old_name,string& new_name){
    int check = rename(old_name.c_str(), new_name.c_str());
    if (check == -1)
        cout<<" Unable to rename";
}

void copier(vector<string> &entities)
{
    int n = entities.size();
    if (n < 3)
    {
        cout<<"Too few arguments!";
        return;
    }
    // cout<<"Got command ";

    string dest = entities[n-1];
    for (int i=1; i<n-1; i++){
        string destnPath = get_path(dest) + "/" + dest;

        string sourcePath = controller.homePath + "/" + entities[i];

        struct stat st;
        bool is_folder=false;
        if (stat(sourcePath.c_str(), &st) == -1){
            cout<<"Cannot open file\n";
            return;
        }
        else{
            if ((S_ISDIR(st.st_mode)))
                is_folder=true;
        }
        if (is_folder == false)
            copy_file(sourcePath, destnPath);
        else{
            string s=entities[i].substr(entities[i].find_last_of('/')+1);
            cout<<(destnPath+"/"+s);
            copy_folder(sourcePath, destnPath+'/'+s);
        }
    }
}

void mover(vector<string> allargs){
    if (allargs.size()<3)
        cout<<"Too few arguments";
    else{
        string dest = get_path(allargs[allargs.size()-1]);
        for (int i=1;i<allargs.size()-1;i++){
            string src=get_path(allargs[i]);  //file
            struct stat st;
            if (stat(src.c_str(), &st) == -1){
                cout<<"Cannot open file";
            }
            else{
                if ((S_ISDIR(st.st_mode)))
                    ;
                else{
                    int ret = mkdir(dest.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
                    string s=dest+"/"+src.substr(src.find_last_of('/')+1);
                    my_rename(src,s);
                }
            }
        }
    }
}

void delete_file(vector<string> &token){
    string file = get_path(token[1]);
    struct stat st;
    if(stat(&file[0], &st) == 0){
        mode_t perm = st.st_mode;
        if( perm & S_IFREG ){
            int ret = remove(file.c_str());
            if (ret == -1)
                cout<<"Could not delete file";
            else
                cout<<"Deleted successfully!";
        } else
            cout<<"Not a file";
    } else
        cout<<"File not found";
}

void goto_path(string newPath,int tot){
    string prev = controller.homePath;
    if (newPath==".")
        newPath=controller.homePath;
    else{
        if (newPath=="..")
            newPath=controller.homePath.substr(0,controller.homePath.find_last_of('/'));
        else
            newPath = get_path(newPath);
        if (initialise(&newPath[0])){
            controller.linenum=0;
            moveCursor(0, 0);
            explorer(0,tot);
            controller.visited.push(prev);
        }
    }
}

void opener(int* pos, int tot){
    if (all[controller.linenum+*pos].perm[0]=='d'){
        controller.visited.push(controller.homePath);
        string newdir;
        if (all[controller.linenum+*pos].name=="..")
            newdir=controller.homePath.substr(0,controller.homePath.find_last_of('/'));
        else if (all[controller.linenum+*pos].name==".")
            newdir=controller.homePath;
        else
            newdir=controller.homePath+"/"+all[controller.linenum+*pos].name;
        initialise(&newdir[0]);
        controller.linenum=0;
        *pos=0;
        moveCursor(0, 0);
        explorer(*pos,tot);
    } 
    else {
        int pid = fork();
        if (pid == 0) {
            string fileName = controller.homePath+'/'+all[controller.linenum+*pos].name;
            execl("/usr/bin/xdg-open", "xdg-open", &fileName[0], (char *)0);
            exit(1);
        }
    }
}

void get_allargs(string rawargs, vector<string> &token)
{
    // Used to split string around spaces.
    string arg = "";
    for (auto x : rawargs)
    {
        if (x == ' ')
        {
            token.push_back(arg);
            arg = "";
        }
        else
        {
            arg += x;
        }
    }
    token.push_back(arg);
}

static void sig_handler(int sig)
{
  if (SIGWINCH == sig) {
    ioctl(0, TIOCGWINSZ, &term);
    disableRaw();
    controller.linenum=0;
    moveCursor(0, 0);
    explorer(0,term.ws_row);
    enableRaw();
  }

} 

  // Capture SIGWINCH
  
int main(){
    signal(SIGWINCH, sig_handler);

    // while (1) {
    //     pause();
    // }
    char c;
    bool normal=true; //false for command mode
    
    char cwd[PATH_MAX];
    getcwd(cwd, PATH_MAX);
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &term);
    initialise(cwd);

    int tot = term.ws_row;
    int pos=0;
    moveCursor(0, 0);
    explorer(pos,term.ws_row);
    // string name="test";
    // copyfile("burr", name, true);
    // delentries("./burr/cony.txt");

    // setFileVector(cwd);
    // printMetaFile();

    while (1) {
        c = '\0';
        enableRaw();
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
        if (c == 'q') {
            clrscr();
            exit(0);
        }
        if (c=='h'){ //home
            char* homedir;
            if ((homedir = getenv("HOME"))==NULL)
                homedir=getpwuid(getuid())->pw_dir;
            disableRaw();
            controller.visited.push(controller.homePath);
            initialise(homedir);
            moveCursor(controller.linenum, 0);
            explorer(pos,term.ws_row);
        }
        else if (iscntrl(c)){
            if (c == '\x1b') {
                char seq[3];
                if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
                if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
                cout<<seq<<endl;
                if (seq[0] == '[') {
                    switch (seq[1]) {
                        case 'A': //up
                            if (controller.linenum==0){
                                if (pos>0)
                                    pos--;
                            } else {
                                controller.linenum--;
                            }
                            disableRaw();
                            moveCursor(controller.linenum, 0);
                            explorer(pos,term.ws_row);
                        break;
                        case 'B': 
                            // cout<<"Down"<<endl;
                            if (pos+controller.linenum<controller.count-1){
                                if (controller.linenum==term.ws_row-5){
                                    pos++;
                                } else {
                                    controller.linenum++;
                                }
                            }
                            disableRaw();
                            moveCursor(controller.linenum, 0);
                            explorer(pos,term.ws_row);
                        break;
                        case 'D': //left
                            disableRaw();
                            if (!controller.visited.empty()){
                                controller.next.push(controller.homePath);
                                initialise(&controller.visited.top()[0]);
                                controller.visited.pop();
                            }
                            moveCursor(controller.linenum, 0);
                            explorer(pos,term.ws_row);
                        break;
                        case 'C': //right
                            disableRaw();
                            if (!controller.next.empty()){
                                controller.visited.push(controller.homePath);
                                initialise(&controller.next.top()[0]);
                                controller.next.pop();
                            }
                            moveCursor(controller.linenum, 0);
                            explorer(pos,term.ws_row);
                        break;
                        default:
                            disableRaw();
                            // moveCursor(linenum, 0);
                            explorer(pos,term.ws_row);
                        break;
                    }
                }
                // return '\x1b';
            } 
            
            if (c==10){ // Enter key
                disableRaw();
                opener(&pos,term.ws_row);
            }
            if (c==127){ //bksp
                controller.visited.push(controller.homePath);
                char* updir = dirname(&controller.homePath[0]);
                disableRaw();
                initialise(updir);
                controller.linenum=2;
                moveCursor(1, 0);
                explorer(pos,term.ws_row);
            }
        }
        else {
            if (c==58){ // : for command mode
                disableRaw();
                normal=false;
                while (!normal){
                    moveCursor(term.ws_row-1, 0);
                    cout << "Command Mode, Press ESC to switch to normal mode\n";
                    string s,comm;
                    
                    char buf;
                    // int ttyfd;
                    
                    // ttyfd = open("/dev/tty", O_RDWR);
                    // if(ttyfd < 0){
                    //     printf("Could not open tty!\n");
                    //     return -1;
                    // }
                    
                    // echoRaw(ttyfd);
                    enableRaw();
                    while(1){
                        fflush(stdin);
                        buf=getchar();
                        if (iscntrl(buf)){
                            if (buf==27){
                                normal=true;
                                disableRaw();
                                break;
                            }
                            else if (buf==10){
                                disableRaw();
                                moveCursor(term.ws_row, 0);
                                cout<<"\33[K\r";
                                break;
                            }
                            else if (buf==127){
                                s.pop_back();
                                disableRaw();
                                cout << '\b' << " " << '\b';
                                enableRaw();
                            }
                        }
                        
                        else {
                            cout<<(char)buf;
                            s+=(char)buf;
                        }
                    }
                    // disRaw(ttyfd);
                    
                    
                    if (normal){
                        cout<<"\033[9999;1H\033[J";
                        moveCursor(term.ws_row-1, 0);
                        cout<<"\33[2K\r";
                        cout << "Normal Mode, Press : to switch to command mode\n";
                        break;
                    }
                    
                    vector<string> allargs;
                    get_allargs(s,allargs);
                    // cout<<"Got command "<<s;
                    if (allargs[0]=="quit"){
                        clrscr();
                        exit(0);
                    }
                    if(allargs[0]=="goto"){
                        goto_path(allargs[1],term.ws_row);
                    }
                    if (allargs[0]=="copy"){
                        copier(allargs);
                    }
                    if (allargs[0]=="rename"){
                        if (allargs.size()<3)
                            cout<<"Too few arguments";
                        else{
                            string old_name=controller.homePath + "/" + allargs[1];
                            string new_name=controller.homePath + "/" + allargs[2];
                            my_rename(old_name,new_name);
                        }
                    }
                    if (allargs[0]=="create_file"){
                        if (allargs.size()<3)
                            cout<<"Too few arguments";
                        else
                            create_file(allargs[1],allargs[2]);
                    }
                    if (allargs[0]=="search"){
                        if (allargs.size()<2)
                            cout<<"Too few arguments";
                        else{
                            pathsearch(&controller.homePath[0],allargs[1]);
                            if (controller.foundFile)
                                controller.foundFile=false;
                            else
                                cout<<"False";
                        }
                    }
                    if (allargs[0]=="delete_file"){
                        delete_file(allargs);
                    }
                    // if (allargs[0]=="delete_dir"){
                        
                    // }
                    if (allargs[0]=="move"){
                        mover(allargs);
                    }
                    if (allargs[0]=="create_dir"){
                        if (allargs.size()<3)
                            cout<<"Too few arguments";
                        else
                            if (create_dir(allargs[1],allargs[2])==0)
                                cout<<"Success";
                    }
                    s="";
                }
            }
            else{
                c = '\0';
                moveCursor(term.ws_row-1, 0);
                disableRaw();
            }
            // printf("%d ('%c')\r\n", c, c);
        }
    }    
            
    return 0;
}