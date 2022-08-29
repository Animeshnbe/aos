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

using namespace std;
#define moveCursor(x, y) std::cout<<"\033["<<(x)<<";"<<(y)<<"H";
#define clrscr() printf("\033[H\033[J");
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
    int MAXN;       // no of files can be printed based on the screen size
    int firstIndex; // point to first index of files
    int lastIndex;  // point to last index of files
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
    controller.firstIndex = 0;
    controller.lastIndex = controller.MAXN;
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


void explorer(int* linenum, int pos, int tot){
    clrscr();
    
    if (*linenum<=0)
        *linenum=0;
    cout<<controller.homePath<<", "<<endl;
    int showlines=controller.count;
    if (controller.count>tot-4){
        //vertical overflow
        showlines=tot-4;
    }

    for (int i=0;i<showlines;i++){
        if (i==*linenum)
            cout<<">>>  "<<all[pos+i].perm<<" "<<setw(3)<<right<< all[pos+i].links<<all[pos+i].og<< setw(5) << right<<all[pos+i].size<<" "<<all[pos+i].ts<<" "<<all[pos+i].name<<endl;
        else
            cout<<"     "<<all[pos+i].perm<<" "<<setw(3)<<right<< all[pos+i].links<<all[pos+i].og<< setw(5) << right<<all[pos+i].size<<" "<<all[pos+i].ts<<" "<<all[pos+i].name<<endl;
    }
    
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

int main(){
    char c;
    bool normal=true; //false for command mode
    
    char cwd[PATH_MAX];
    getcwd(cwd, PATH_MAX);
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &term);
    initialise(cwd);

    int tot = term.ws_row;
    int linenum=0,pos=0;
    moveCursor(0, 0);
    explorer(&linenum,pos,tot);
    // string name="test";
    // copyfile("burr", name, true);
    // delentries("./burr/cony.txt");

    // setFileVector(cwd);
    // printMetaFile();

    while (1) {
        char c = '\0';
        enableRaw();
        if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
        if (c == 'q') break;
        else if (iscntrl(c) && c==27){
            if (c == '\x1b') {
                char seq[3];
                if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
                if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
                if (seq[0] == '[') {
                    switch (seq[1]) {
                        case 'A':
                            if (linenum==0){
                                if (pos>0)
                                    pos--;
                            } else {
                                linenum--;
                            }
                            disableRaw();
                            moveCursor(linenum, 0);
                            explorer(&linenum,pos,tot);
                        break;
                        case 'B': 
                            // cout<<"Down"<<endl;
                            if (pos+linenum<controller.count-1){
                                if (linenum==tot-5){
                                    pos++;
                                } else {
                                    linenum++;
                                }
                            }
                            disableRaw();
                            moveCursor(linenum, 0);
                            explorer(&linenum,pos,tot);
                        break;
                        case 'D': 
                            disableRaw();
                            if (!controller.visited.empty()){
                                controller.next.push(controller.homePath);
                                initialise(&controller.visited.top()[0]);
                                controller.visited.pop();
                            }
                            moveCursor(linenum, 0);
                            explorer(&linenum,pos,tot);
                        break;
                        case 'C':
                            disableRaw();
                            if (!controller.next.empty()){
                                controller.visited.push(controller.homePath);
                                initialise(&controller.next.top()[0]);
                                controller.next.pop();
                            }
                            moveCursor(linenum, 0);
                            explorer(&linenum,pos,tot);
                        break;
                    }
                }
                // return '\x1b';
            } 
        }
        else {
            if (c==10){
                cout<<"Enter";
            }
            if (c=='h'){
                char* homedir;
                if ((homedir = getenv("HOME"))==NULL)
                    homedir=getpwuid(getuid())->pw_dir;
                disableRaw();
                controller.visited.push(controller.homePath);
                initialise(homedir);
                moveCursor(linenum, 0);
                explorer(&linenum,pos,tot);
            }
            if (c==127){
                char* updir = dirname(&controller.homePath[0]);
                disableRaw();
                controller.visited.push(controller.homePath);
                initialise(updir);
                moveCursor(linenum, 0);
                explorer(&linenum,pos,tot);
            }
            // printf("%d ('%c')\r\n", c, c);
        }
    }            
            

        // cout<<(int)c<<endl;
        // if ((c == 'k' || c == 'K') && controller.firstIndex != 0)
        // {
        // }

        // if (c==127){
        //     uplvl();
        // }
        // if (c==68){
        //     back();
        // }
        // if (c==67){
        //     forward();
        // }
    return 0;
}