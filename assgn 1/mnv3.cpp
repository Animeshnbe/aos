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
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <limits.h>
#include <ftw.h>

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
    int maxuserlen=0;
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
bool normal=true; //false for command mode
int pos=0;

struct termios orig_termios,original;

void disableRaw(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void enableRaw(){
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRaw);

    struct termios raw = orig_termios;

    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
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

string get_path(string token){
    string currentdirpath = controller.homePath;
    if (currentdirpath=="/" && token.length()>0){
        if (token==".." || token==".")
            return currentdirpath;
        currentdirpath = "";
    }

    int i;
    for (i=0;i<token.length();i++){
        if (token[i]=='~'){
            char* homedir;
            if ((homedir = getenv("HOME"))==NULL)
                homedir=getpwuid(getuid())->pw_dir;
            currentdirpath = homedir;
        }
        else if (token[i]=='.'){ 
            if (token[i+1] != '\0'){
                if (token[i+1] == '.'){
                    if (i==0){
                        if (currentdirpath!="/")
                            currentdirpath = currentdirpath.substr(0,currentdirpath.find_last_of('/'));
                    }
                    else if (currentdirpath[currentdirpath.length()-1]=='/'){
                        if (currentdirpath!="/"){
                            currentdirpath = currentdirpath.substr(0,currentdirpath.find_last_of('/'));
                            currentdirpath = currentdirpath.substr(0,currentdirpath.find_last_of('/'));
                        }
                    }
                    else
                        currentdirpath += '.';
                    i++; //move 2 places
                }
                else if (token[i+1]=='/'){
                    if (i==0)
                        currentdirpath += '/';
                    i++;
                } else {
                    currentdirpath += token[i];
                }
            }
            else{
                return currentdirpath;
            }
        }
        else if (token[i]=='/'){
            if (i==0)
                currentdirpath = "/";
            else if (token[i+1] != '\0')
                currentdirpath += token[i];
        }else{
            if (i==0)
                currentdirpath += "/";
            currentdirpath += token[i];
        }
    }
    return currentdirpath;
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
        // cout<<"Could not open this directory "<<dirname;
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
    int maxlen = 0;
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
            if (maxlen<fs.og.length())
                maxlen=fs.og.length();
            fs.links = links(nm);
            fs.size = readableSize(file_stats.st_size);
            fs.ts = ts_act.substr(4,12);
            fs.name = files[i];
            all.push_back(fs);
        }
    }
    controller.maxuserlen = maxlen;
    closedir(dir);
    return true;
}


void explorer(int pos, int tot){
    clrscr();
    
    if (controller.linenum<=0)
        controller.linenum=0;

    if (controller.homePath.length()+6+to_string(controller.count).length()<term.ws_col)
        cout<<controller.homePath<<", Tot "<<controller.count<<endl;
    else
        cout<<controller.homePath.substr(0,4)<<"..., Tot "<<controller.count<<endl;
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
        if (i==controller.linenum){
            cout<<"\033[1;31m";
            cout<<">>>  ";
        }
        else
            cout<<"     ";
        if (39+controller.maxuserlen+all[pos+i].name.length()>term.ws_col){
            if (44+controller.maxuserlen<term.ws_col){
                if (all[pos+i].name.length()<3)
                    cout<<all[pos+i].perm<<" "<<setw(3)<<right<< all[pos+i].links<<setw(controller.maxuserlen+1)<<all[pos+i].og<< setw(5) << right<<all[pos+i].size<<" "<<all[pos+i].ts<<" "<<all[pos+i].name;
                else
                    cout<<all[pos+i].perm<<" "<<setw(3)<<right<< all[pos+i].links<<setw(controller.maxuserlen+1)<<all[pos+i].og<< setw(5) << right<<all[pos+i].size<<" "<<all[pos+i].ts<<" "<<all[pos+i].name.substr(0,3)<<"...";
            }
            else if (27<term.ws_col){
                if (all[pos+i].name.length()<3)
                    cout<<all[pos+i].perm.substr(0,4)<<"... "<<all[pos+i].og.substr(0,3)<<"... "<<all[pos+i].name;
                else
                    cout<<all[pos+i].perm.substr(0,4)<<"... "<<all[pos+i].og.substr(0,3)<<"... "<<all[pos+i].name.substr(0,3)<<"...";
            }
            else if (20<term.ws_col){
                if (all[pos+i].name.length()<3)
                    cout<<all[pos+i].perm.substr(0,4)<<"... "<<all[pos+i].name;
                else
                    cout<<all[pos+i].perm.substr(0,4)<<"... "<<all[pos+i].name.substr(0,3)<<"...";
            }
            else
                cout<<all[pos+i].perm<<" "<<setw(3)<<right<< all[pos+i].links<<setw(controller.maxuserlen+1)<<all[pos+i].og<< setw(5) << right<<all[pos+i].size<<" "<<all[pos+i].ts<<" "<<all[pos+i].name;    
        }
        else
            cout<<all[pos+i].perm<<" "<<setw(3)<<right<< all[pos+i].links<<setw(controller.maxuserlen+1)<<all[pos+i].og<< setw(5) << right<<all[pos+i].size<<" "<<all[pos+i].ts<<" "<<all[pos+i].name;
        cout<<"\033[0m";
        printf("\r\n");
    }

    moveCursor(tot-1, 0);
    if (46<term.ws_col)
        cout << "Normal Mode, Press : to switch to command mode\n";
    else
        cout << "Normal Mode\n";
    
}

void refresh(){
    initialise(controller.homePath.c_str());
    moveCursor(1, 0);
    explorer(pos,term.ws_row);
}

void pathsearch(char* dirname, string name){
    struct dirent *dent;
    DIR *dir = opendir(dirname);
  
    if (dir == NULL)
        return;

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

void create_file(string& name,string dest){
    fstream file;
    file.open(get_path(dest)+"/"+name,ios::out | ios::app);
    if(!file){
        fstream file(name, fstream::in | fstream::out | fstream::trunc);
    }
    refresh();
    cout<<"File created!";
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

bool copy_file(string src, string dst){
    struct stat srcstat;
    if (stat(src.c_str(), &srcstat) == -1){
        cout<<"Cannot open file";
        return false;
    }
    ifstream source(src, ios::binary);
    ofstream dest(dst, ios::binary);

    dest << source.rdbuf();

    source.close();
    dest.close();
    chown(&dst[0], srcstat.st_uid, srcstat.st_gid);
    // cout<<permissions(&dst[0])<<", ";
    chmod(&dst[0], srcstat.st_mode);
    return true;
}

bool copy_folder(string src, string dest){
    DIR *direc;
    struct dirent *d;
    int ret = mkdir(dest.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); //make if not present
    // cout<<src<<", "<<dest;
    direc = opendir(src.c_str());
    if (direc == NULL){
        // cout<<"Cannot open source";
        return false;
    }

    bool b=true;
    while ((d = readdir(direc)) != NULL){
        if (strcmp(d->d_name,".") && strcmp(d->d_name,"..")){
            string name = d->d_name;
            string eachfile = src + "/" + d->d_name;
            struct stat st;
            if (stat(eachfile.c_str(), &st) == -1)
                return false;

            if (S_ISDIR(st.st_mode)){
                b = b && copy_folder(eachfile, dest + "/" + d->d_name);
            }
            else
                b = b && copy_file(eachfile, dest + "/" + d->d_name);
        }
    }
    return b;
}

void my_rename(string& old_name,string& new_name){
    int check = rename(old_name.c_str(), new_name.c_str());
    if (check == -1)
        cout<<" Unable to rename";
    else{
        refresh();
        cout<<"Renamed to "<<new_name;
        // controller.linenum=
    }
}

void copier(vector<string> &entities){
    int n = entities.size();
    if (n < 3){
        cout<<"Too few arguments!";
        return;
    }
    // cout<<"Got command ";
    string dest = entities[n-1];
    string destnPath = get_path(dest);
    vector<string> cp_f;
    bool cp_s=false;
    for (int i=1; i<n-1; i++){
        
        string sourcePath = get_path(entities[i]);

        struct stat st;
        bool is_folder=false;
        if (stat(sourcePath.c_str(), &st) == -1){
            // cout<<"Cannot open dir entry";
            cp_f.push_back(entities[i]);
            continue;
        }
        else{
            if ((S_ISDIR(st.st_mode)))
                is_folder=true;
        }
        string s=entities[i].substr(entities[i].find_last_of('/')+1);
        if (is_folder == false){
            bool cp_stat = copy_file(sourcePath, destnPath+"/"+s);
            if (!cp_stat)
                cp_f.push_back(entities[i]);
            cp_s = cp_s || cp_stat;
        }
        else{
            pathsearch(&sourcePath[0],destnPath.substr(destnPath.find_last_of('/')+1));
            if (controller.foundFile){
                moveCursor(term.ws_row, 0);
                cout<<"Cannot copy a folder into itself!";
                controller.foundFile = false;
                cp_f.push_back(entities[i]);
                continue;
            } else {
                bool cp_stat = copy_folder(sourcePath, destnPath+"/"+s);
                if (!cp_stat)
                    cp_f.push_back(entities[i]);
                cp_s = cp_s || cp_stat;
            }
        }
    }
    refresh();
    if (cp_f.size()==0){
        if (cp_s)
            cout<<"Copied Successfully";
        else
            cout<<"No file/directory to copy";
    } else {
        string failed="";
        for (int f=0;f<cp_f.size();f++)
            failed+=(cp_f[f]+", ");
        failed.pop_back();
        failed.pop_back();
        if (failed.length()+12<term.ws_col)
            cout<<"Cannot copy "<<failed;
        else
            cout<<"Cannot copy "<<failed.substr(0,term.ws_col-12);
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
            else{
                refresh();
                cout<<"Deleted successfully!";
            }
        } else
            cout<<"Not a file";
    } else
        cout<<"File not found";
}

static int delete_dir(const char *pathname, const struct stat *sbuf, int type, struct FTW *ftwbuf){
    if(pathname=="." || pathname==".."){
        perror("Cannot delete here");
        return -1;
    }
    if (remove(pathname) < 0)
    {
        perror("Cannot delete due to permission error");
        return -1;
    }
    return 0;
}

void mover(vector<string> allargs){
    if (allargs.size()<3)
        cout<<"Too few arguments";
    else{
        string dest = get_path(allargs[allargs.size()-1]);
        vector<string> mv_f;
        bool mv_s=false;
        for (int i=1;i<allargs.size()-1;i++){
            string src=get_path(allargs[i]);  //file
            struct stat st;
            if (stat(src.c_str(), &st) == -1){
                mv_f.push_back(allargs[i]);
            }
            else{
                string s=dest+"/"+src.substr(src.find_last_of('/')+1);
                // cout<<"DEST == "<<s;
                if ((S_ISDIR(st.st_mode))){
                    pathsearch(&src[0],dest.substr(dest.find_last_of('/')+1));
                    if (controller.foundFile){
                        moveCursor(term.ws_row, 0);
                        cout<<"Cannot move a folder into itself!";
                        mv_f.push_back(allargs[i]);
                        controller.foundFile = false;
                        continue;
                    } else {
                        bool mv_stat = copy_folder(src, s);
                        if (mv_stat){
                            if (nftw(src.c_str(), delete_dir,10, FTW_DEPTH|FTW_MOUNT|FTW_PHYS) < 0){
                                perror("File Tree walk error");
                                mv_f.push_back(allargs[i]);
                                continue;
                            }
                            mv_s=true;
                        } else
                            mv_f.push_back(allargs[i]);
                    }
                }
                else{
                    int ret = mkdir(dest.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
                    int check = rename(src.c_str(), s.c_str());
                    if (check == -1)
                        mv_f.push_back(allargs[i]);
                    else
                        mv_s=true;
                }
            }
        }
        refresh();
        if (mv_f.size()==0){
            if (mv_s)
                cout<<"Moved Successfully";
            else
                cout<<"No file/directory to move";
        } else {
            string failed="";
            for (int f=0;f<mv_f.size();f++)
                failed+=(mv_f[f]+", ");
            failed.pop_back();
            failed.pop_back();
            if (failed.length()+12<term.ws_col)
                cout<<"Cannot move "<<failed;
            else
                cout<<"Cannot move "<<failed.substr(0,term.ws_col-12);
        }
    }
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
            pos=0;
            moveCursor(0, 0);
            explorer(0,tot);
            cout<<"PATH> "<<newPath;
            controller.visited.push(prev);
            while(!controller.next.empty())
                controller.next.pop();
        }
    }
}

void opener(int* pos, int tot){
    if (all[controller.linenum+*pos].name=="." || (all[controller.linenum+*pos].name==".." && controller.homePath=="/"))
        return;
    if (all[controller.linenum+*pos].perm[0]=='d'){
        controller.visited.push(controller.homePath);
        string newdir;
        if (all[controller.linenum+*pos].name=="..")
            newdir=controller.homePath.substr(0,controller.homePath.find_last_of('/'));
        else
            newdir=controller.homePath+"/"+all[controller.linenum+*pos].name;
        while(!controller.next.empty())
            controller.next.pop();
        initialise(&newdir[0]);
        controller.linenum=0;
        *pos=0;
        moveCursor(0, 0);
        explorer(*pos,tot);
    } 
    else {
        int pid = fork();
        if (pid == 0) {
            string fileName = controller.homePath+"/"+all[controller.linenum+*pos].name;
            execl("/usr/bin/xdg-open", "xdg-open", &fileName[0], (char *)0);
            exit(1);
        }
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

static void sig_handler(int sig){
  if (SIGWINCH == sig) {
    ioctl(0, TIOCGWINSZ, &term);
    disableRaw();
    controller.linenum=0;
    moveCursor(0, 0);
    explorer(0,term.ws_row);
    if (!normal){
        moveCursor(term.ws_row-1, 0);
        cout<<"\33[2K\r";
        if (48>=term.ws_col)
            cout << "Command Mode\n";
        else
            cout << "Command Mode, Press ESC to switch to normal mode\n";
    }
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
    
    char cwd[PATH_MAX];
    getcwd(cwd, PATH_MAX);
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &term);
    initialise(cwd);

    moveCursor(0, 0);
    explorer(pos,term.ws_row);
    // string name="test";
    // copyfile("burr", name, true);
    // delentries("./burr/cony.txt");

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
            if (controller.homePath!=homedir){
                disableRaw();
                controller.visited.push(controller.homePath);
                while(!controller.next.empty())
                    controller.next.pop();
                initialise(homedir);
                moveCursor(controller.linenum, 0);
                explorer(pos,term.ws_row);
            }
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
                disableRaw();
                if (controller.homePath!="/"){
                    controller.visited.push(controller.homePath);
                    while(!controller.next.empty())
                        controller.next.pop();
                    char* updir = dirname(&controller.homePath[0]);
                    
                    initialise(updir);
                    controller.linenum=0;
                    moveCursor(1, 0);
                    explorer(pos,term.ws_row);
                }
            }
        }
        else {
            if (c==58){ // : for command mode
                disableRaw();
                normal=false;
                while (!normal){
                    moveCursor(term.ws_row-1, 0);
                    if (48<term.ws_col)
                        cout << "Command Mode, Press ESC to switch to normal mode\n";
                    else
                        cout << "Command Mode\n";
                    string s,comm;
                    
                    char buf;
                    enableRaw();
                    bool started=false;
                    while(1){
                        fflush(stdin);
                        buf=getchar();
                        if (!started){
                            cout<<"\33[K\r";
                            started=true;
                            if (buf=='['){   // if last switch from cmd done using keys other than ESC
                                buf=getchar();
                                continue;
                            }
                        }
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
                    
                    
                    if (normal){
                        cout<<"\033[9999;1H\033[J";
                        moveCursor(term.ws_row-1, 0);
                        cout<<"\33[2K\r";
                        if (46<term.ws_col)
                            cout << "Normal Mode, Press : to switch to command mode\n";
                        else
                            cout << "Normal Mode\n";
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
                        else{
                            create_file(allargs[1],allargs[2]);
                        }
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
                    if (allargs[0]=="delete_dir"){
                        if (allargs.size()<2)
                            cout<<"Too few arguments";
                        else{
                            string delete_path = get_path(allargs[1]);
                            if (delete_path == "/" || delete_path == controller.homePath || strcmp(delete_path.c_str(), cwd)==0){
                                cout<<"Cannot delete this folder";
                            }
                            else if (nftw(delete_path.c_str(), delete_dir,10, FTW_DEPTH|FTW_MOUNT|FTW_PHYS) < 0){
                                perror("File Tree walk error");
                            } else {
                                refresh();
                                cout<<"Deleted successfully";
                            }
                        }
                    }
                    if (allargs[0]=="move"){
                        mover(allargs);
                    }
                    if (allargs[0]=="create_dir"){
                        if (allargs.size()<3)
                            cout<<"Too few arguments";
                        else{
                            if (create_dir(allargs[1],allargs[2])==0){
                                refresh();
                                cout<<"Success";
                            }
                        }
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
