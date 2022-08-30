#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <unistd.h>
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
    bool foundFile;
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

struct termios orig_termios;

void disableRaw(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void enableRaw(){
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRaw);

    struct termios raw = orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
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

void initialise(const char *dirname)
{
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &term); // initalize value of w based on screen
    controller.cursorptr = 1;
    controller.homePath = dirname;

    struct dirent *dent;
    DIR *dir = opendir(dirname);
  
    if (dir == NULL)
        cout<<"Startup error: Could not open current directory";

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
}


void explorer(int pos, int tot){
    clrscr();
    
    if (controller.linenum<=0)
        controller.linenum=0;
    cout<<controller.homePath<<", "<<tot<<endl;
    // if (controller.visited.empty())
    //     cout<<endl;
    // else
    //     cout<<controller.visited.top()<<endl;
    int showlines=controller.count;
    if (controller.count>tot-4){
        //vertical overflow
        showlines=tot-4;
    }

    for (int i=0;i<showlines;i++){
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
            if (strcmp(dirname,".")==0)
                cout<<"Results in current directory ";
            else
                cout<<"Results in "<<dirname<<" ";
            cout<<dent->d_name<<endl;
        }
        if (dent->d_type==DT_DIR && strcmp(dent->d_name,".")!=0 && strcmp(dent->d_name,"..")!=0){
            char path[100] = {0};
            strcat(path,dirname);
            strcat(path,"/");
            strcat(path,dent->d_name);
            pathsearch(path,name);
        }
    }
}

void uplvl(){

}

void back(){

}

void forward(){

}

void copyfile(string src, string dst,bool move){
    clock_t start, end;
    start = clock();

    try
    {
        struct dirent *dent;
        struct stat st;
        if(stat(&src[0], &st) == 0){
            mode_t enttype = st.st_mode;
            filesystem:: path sourceFile = src;
            filesystem:: path targetParent = dst;
            // auto target = targetParent / sourceFile.filename();
            filesystem:: copy(sourceFile, targetParent, filesystem ::copy_options::overwrite_existing | filesystem::copy_options::recursive);
            if (move==true){
                uintmax_t n = filesystem::remove_all(sourceFile);
            }
        }
    }
    catch (std::exception& ex) //If any filesystem error
    {
        cout << ex.what();
    }

    end = clock();

    cout << "COPIED in " << static_cast<double>(end - start) / CLOCKS_PER_SEC << " seconds\n";
}

void delentries(string dirname){
    filesystem:: path tmp = dirname;
    uintmax_t n = filesystem::remove_all(tmp);
    cout<<"Deleted "<<n<<" file(s) and directory(ies)"<<endl;
}

void rename(string src, string newPath){
    filesystem::path p = src;
    filesystem::rename(p, newPath);
 
    // fs::remove_all(src);
}

void goto_path(string newPath,int tot){
    controller.visited.push(controller.homePath);
    initialise(&newPath[0]);
    controller.linenum=0;
    moveCursor(0, 0);
    explorer(0,tot);
}

void opener(int* pos, int tot){
    if (all[controller.linenum+*pos].perm[0]=='d'){
        controller.visited.push(controller.homePath);
        string newdir=controller.homePath+'/'+all[controller.linenum+*pos].name;
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
        if (c == 'q') break;
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
                        case 'A':
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
                        case 'D': 
                            disableRaw();
                            if (!controller.visited.empty()){
                                controller.next.push(controller.homePath);
                                initialise(&controller.visited.top()[0]);
                                controller.visited.pop();
                            }
                            moveCursor(controller.linenum, 0);
                            explorer(pos,term.ws_row);
                        break;
                        case 'C':
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
            
            if (c==13){
                disableRaw();
                opener(&pos,term.ws_row);
            }
            if (c==127){ //bksp
                controller.visited.push(controller.homePath);
                char* updir = dirname(&controller.homePath[0]);
                disableRaw();
                initialise(updir);
                controller.linenum=2;
                moveCursor(2, 0);
                explorer(pos,term.ws_row);
            }
        }
        else {
            if (c==58){
                disableRaw();
                fflush(stdin);
                normal=false;
                while (!normal){
                    moveCursor(term.ws_row-1, 0);
                    cout << "Command Mode, Press ESC to switch to normal mode\n";
                    string s,comm;
                    
                    char buf;
                    struct termios original, newt;
                    int ttyfd;
                    
                    ttyfd = open("/dev/tty", O_RDWR);
                    if(ttyfd < 0){
                        printf("Could not open tty!\n");
                        return -1;
                    }
                    
                    tcgetattr(ttyfd, &original);
                    newt=original;
                    newt.c_lflag &= ~(ICANON);
                    tcsetattr(ttyfd, TCSANOW, &newt);
                    
                    while(1){
                        if (read(STDIN_FILENO, &buf, 1) == -1 && errno != EAGAIN) die("read");
                        if (iscntrl(buf)){
                            if (buf==27){
                                cout<<"\033[9999;1H\033[J";
                                cin.clear();
                                normal=true;
                                break;
                            }
                            else if (buf==10){
                                cout<<"M: "<<s<<endl;
                                break;
                            }
                        }
                        
                        else {
                            s+=(char)buf;
                        }
                    }
                    tcsetattr(ttyfd, TCSANOW, &original);
                    
                    close(ttyfd);
                    if (normal){
                        moveCursor(term.ws_row-1, 0);
                        cout << "Normal Mode, Press : to switch to command mode\n";
                        break;
                    }
                    comm = s.substr(0, s.find(' '));
                    int i=s.find(' ')+1;
                    
                    if(comm=="goto"){
                        goto_path(s.substr(i),term.ws_row);
                    }
                }
            }
            // printf("%d ('%c')\r\n", c, c);
        }
    }    
            
    return 0;
}