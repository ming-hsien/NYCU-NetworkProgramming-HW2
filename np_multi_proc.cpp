#include <iostream>
#include <string.h>
#include <sstream>
#include <map>
#include <vector>
#include <queue>
#include <regex>

#include <stdio.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>

// this is for open file for file descriptor
#include <fcntl.h>
// this is for construct socket
#include <arpa/inet.h>

using namespace std;

#define _DEBUG_ 0
#define BUFSIZE 15000
#define MAX_CLIENT 32 // gow many client can connect server

int status;
int SERV_TCP_PORT = 7001;
const int FD_NULL = open("/dev/null", O_RDWR);

#define SHMKEY_Msg 7890
#define SHMKEY_ClientInfo 7891
#define SHMKEY_UserPipe 7892

int SHMMsgId, SHMClientInfoId, SHMUserPipeId;
int ClientID;

struct PipeFd {
    vector<pid_t> PipeFromPids;
    int PipeFdNumber[2] = {-1, -1};
};

struct UserPipeFd {
    int PipeFdNumber[MAX_CLIENT];
};

struct Client {
    char IP[INET_ADDRSTRLEN];
    pid_t pid;
    int INPort;
    char username[30];
    bool alive;
};

// client private pipemap
map<int, PipeFd> PIPEMAP;
int timestamp = 0;
// Shared Parameter
Client* CLIENTMAP; // client repository
char* CLIENTMsg; // broadcastMsg
UserPipeFd* USERPIPEMAP; // Stored Userpipe

struct CMDtype {
    string BIN = "";
    int PipeN = 0;
    vector<string> param;
    bool Is_StdError = false;
    bool Is_PipeCmd = false;
    bool Is_FileReDirection = false;
    string writePath = "";
    bool Is_NumPipeCmd = false;
    string CmdStr = "";
    // This is for user pipe! if some body want to 
    bool Is_ReadUserPipe = false;
    bool Is_WriteUserPipe = false;
    int UserPipeTo = 0;
    int UserPipeFrom = 0;
};

void init();

bool isStdError(string Cmd) {
    if (Cmd.find('!') != string::npos) 
        return true;
    return false;
}

bool isPipeCmd(string Cmd) {
    if (Cmd.find('|') != string::npos || Cmd.find('!') != string::npos)
        return true;
    return false;
}

bool isNumPipeCmd(string Cmd) {
    if ((Cmd.find('|') != string::npos || Cmd.find('!') != string::npos) && isdigit(Cmd[Cmd.length() - 1]))
        return true;
    return false;
}

bool isErrorPipeCmd(string Cmd) {
    if (Cmd.find('!') != string::npos)
        return true;
    return false;
}

bool isFileRedirectCmd(string Cmd) {
    if (Cmd.find(" > ") != string::npos)
        return true;
    return false;
}

bool isReadUserPipe(string Cmd) {
    regex reg(".*<[0-9]+(.|\n)*");
    return regex_match(Cmd, reg);
}

bool isWriteUserPipe(string Cmd) {
    regex reg(".*>[0-9]+(.|\n)*");
    return regex_match(Cmd, reg);
}

string vector2String(vector<string> v, int fromwhere = 0) {
    string v2s = v[fromwhere];
    for (int i = fromwhere + 1; i < v.size(); i++) {
        v2s += (" " + v[i]);
    }
    return v2s;
} 

vector<string> splitEachCmd2GeneralOrNonOrNumPipeCmds(string eachCmd) {
    vector<string> PerNonNumPipeCmd;
    stringstream ss;
    string s1;
    ss.clear();
    ss << eachCmd;
    string buffer = "";
    while (true) {
        ss >> s1;
        if (ss.fail())
            break;

        buffer = buffer + " " + s1;

        if (s1.find("|") != string::npos || s1.find("!") != string::npos) {
            PerNonNumPipeCmd.push_back(buffer);
            buffer = "";
        }
        else
            continue;
    }
    if (buffer != "")
        PerNonNumPipeCmd.push_back(buffer);

    #if _DEBUG_ == 1
    cout << "============================" << endl;
    cout << "this is the split Each cmd to Non numPipe command of the your input." << endl;
    for (int i = 0; i < PerNonNumPipeCmd.size(); i++)
        cout << PerNonNumPipeCmd[i] << endl;
    cout << "============================" << endl;
    #endif

    return PerNonNumPipeCmd;
}

vector<string> splitLine2EachCmd(vector<string> cmd)
{
    pid_t pid;
    string buffer = "";
    string prevStr = "";
    vector<string> cmdVec;
    for (int i = 0; i < cmd.size(); i++) {
        if (isNumPipeCmd(cmd[i])) {
            cmdVec.push_back(buffer + " " + cmd[i]);
            buffer = "";
        }
        else {
            buffer == "" ? buffer = cmd[i] : buffer = buffer + " " + cmd[i];
        }
            
    }
    if (buffer != "")
        cmdVec.push_back(buffer);

    // this is for comfirm the split of the command.
    #if _DEBUG_ == 1
    cout << "============================" << endl;
    cout << "this is the split line to each cmd of the your input." << endl;
    for (int i = 0; i < cmdVec.size(); i++)
        cout << cmdVec[i] << endl;
    cout << "============================" << endl;
    #endif

    return cmdVec;
}

vector<string> splitLineSpace(string cmd) {
    vector<string> v;
    stringstream ss;
    string s1;
    ss.clear();
    ss << cmd;

    while (true) {
        ss >> s1;
        if (ss.fail())
            break;
        v.push_back(s1);
    }
    return v;
}

char **vectorToCharPointer(vector<string> &src) {
	int argc = src.size();
	char **argv = new char*[argc+1]; 
	for(int i = 0; i < argc; i++){
		argv[i] = new char[src[i].size()];
		strcpy(argv[i], src[i].c_str());
	}
	argv[argc] = new char;
	argv[argc] = NULL;
	return argv;
}

int getPipeTimes(string cmdLine) {
    int PipeTimes = 0;
    int CutPoint = 0;
    for (int i = 0; i < cmdLine.length(); i++) {
        if (cmdLine[i] == '|' || cmdLine[i] == '!') {
            if (i + 1 < cmdLine.length() && isdigit(cmdLine[i + 1])) {
                int l = i + 1;
                while (l < cmdLine.length() && isdigit(cmdLine[l])) {
                    PipeTimes *= 10;
                    PipeTimes += cmdLine[l] - '0';
                    l++;
                }
            }
        }
    }
    return PipeTimes;
}

int getUserPipefromWho(vector<string> cmdLineVec) {
    int fromWho = 0;
    for (int i = 0; i < cmdLineVec.size(); i++) {
        if (cmdLineVec[i][0] == '<') {
            for (int k = 1; k < cmdLineVec[i].size(); k++) {
                fromWho = fromWho * 10 + (cmdLineVec[i][k] - '0');
            }
        }
    }
    return fromWho;
}

int getUserPipeToWho(vector<string> cmdLineVec) {
    int toWho = 0;
    for (int i = 0; i < cmdLineVec.size(); i++) {
        if (cmdLineVec[i][0] == '>') {
            for (int k = 1; k < cmdLineVec[i].size(); k++) {
                toWho = toWho * 10 + (cmdLineVec[i][k] - '0');
            }
        }
    }
    return toWho;
}

void shudwn(int signo) {
    if (signo != SIGINT) return;
    shmdt(CLIENTMAP);
    shmdt(CLIENTMsg);
    shmdt(USERPIPEMAP);
    shmctl(SHMClientInfoId, IPC_RMID, (struct shmid_ds*) 0);
    shmctl(SHMMsgId, IPC_RMID, (struct shmid_ds*) 0);
    shmctl(SHMUserPipeId, IPC_RMID, (struct shmid_ds*) 0);
    exit(0);
}

void signalHandler(int signo) {
    while(waitpid(-1, &status, WNOHANG) > 0);
}

// when SIGUSR1 issue
void outputBMsg(int signo) {
    if (signo != SIGUSR1) return;
    cout << CLIENTMsg;
}

// signal open fifo
void sigFifo(int signo) {
    if (signo != SIGUSR2) return;
    char FIFOName[20];
    for(int i = 1; i < MAX_CLIENT; i++) {
        if (USERPIPEMAP[i].PipeFdNumber[ClientID] == -2) {
            memset(FIFOName, '\0', sizeof(FIFOName));
            sprintf(FIFOName, "/tmp/userPipe-%d-%d", i, ClientID);
            USERPIPEMAP[i].PipeFdNumber[ClientID] = open(FIFOName, O_RDONLY | O_NONBLOCK);
        }
    }
}

void BroadcastMessage(string msg) {
    memset(CLIENTMsg, '\0', sizeof(CLIENTMsg));
    strcpy(CLIENTMsg, msg.c_str());
    for (int i = 1; i < MAX_CLIENT; i++) {
        if (!CLIENTMAP[i].alive) continue;
        kill(CLIENTMAP[i].pid, SIGUSR1);
    }
    usleep(2000);
}

// Process who command
void who(int ID) {
    string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
    for (int i = 1; i < MAX_CLIENT; i++) {
        if (!CLIENTMAP[i].alive) continue;
        if (i == ID) {
            msg = msg + to_string(i) + '\t' + string(CLIENTMAP[i].username) + '\t' + CLIENTMAP[i].IP + ":" + to_string(CLIENTMAP[i].INPort) + "\t<-me\n";
        }
        else {
            msg = msg + to_string(i) + '\t' + string(CLIENTMAP[i].username) + '\t' + CLIENTMAP[i].IP + ":" + to_string(CLIENTMAP[i].INPort) + "\n";
        }
    }
    cout << msg;
}

// Process tell command
void tell(int fromID, int toWhoID, string msg) {
    // Not Found specific client
    if (!CLIENTMAP[toWhoID].alive) {
        string NotFoundMsg = "*** Error: user #" + to_string(toWhoID) + " does not exist yet. ***\n";
        cout << NotFoundMsg;
    }
    // Find and send msg to specific client
    else {
        string retMsg = "*** " + string(CLIENTMAP[fromID].username) + " told you ***: " + msg + "\n";
        memset(CLIENTMsg, '\0', sizeof(CLIENTMsg));
        strcpy(CLIENTMsg, retMsg.c_str());
        kill(CLIENTMAP[toWhoID].pid, SIGUSR1);
    }
}

// Process yell command
void yell(int fromID, string msg) {
    string yellMsg = "*** " + string(CLIENTMAP[fromID].username) + " yelled ***: " + msg + "\n";
    BroadcastMessage(yellMsg);
}

// Process name command
void name(int userID, string newname) {
    // Can not have two client have same user name.
    bool repeat = false;
    for (int i = 1; i < MAX_CLIENT; i++) {
        if (CLIENTMAP[i].username == newname) {
            repeat = true;
            break;
        }
    }
    if (repeat) {
        string msg = "*** User '" + newname + "' already exists. ***\n";
        cout << msg;
    }
    else {
        string IP_ = CLIENTMAP[userID].IP;
        memset(CLIENTMAP[userID].username, '\0', sizeof(CLIENTMAP[userID].username));
        strcpy(CLIENTMAP[userID].username, newname.c_str());
        string broadcastMsg = "*** User from " + IP_ + ":" + to_string(CLIENTMAP[userID].INPort) + " is named '" + newname + "'. ***\n";
        BroadcastMessage(broadcastMsg);
    }
}

// return a type of CMD structure
CMDtype pareseOneCmd(string Cmd) {
    CMDtype CmdPack;
    CmdPack.Is_StdError = isStdError(Cmd);
    CmdPack.Is_PipeCmd = isPipeCmd(Cmd);
    CmdPack.Is_FileReDirection = isFileRedirectCmd(Cmd);
    CmdPack.Is_NumPipeCmd = isNumPipeCmd(Cmd);
    CmdPack.Is_ReadUserPipe = isReadUserPipe(Cmd);
    CmdPack.Is_WriteUserPipe = isWriteUserPipe(Cmd);
    
    vector<string> v = splitLineSpace(Cmd);
    CmdPack.BIN = v[0];
    v.erase(v.begin());
    if (v.size() > 0 && CmdPack.Is_PipeCmd) {
        CmdPack.PipeN = getPipeTimes(Cmd);
        v.pop_back();
    }
    CmdPack.CmdStr += CmdPack.BIN;

    // if current command is Read user pipe
    if (CmdPack.Is_ReadUserPipe) {
        if ((CmdPack.UserPipeFrom = getUserPipefromWho(v)) == 0) {
            #if _DEBUG_ == 1
            cerr << "Error : getUserPipeFrom Who error";
            #endif
        }
    }
    // if current command is Write user pipe
    if (CmdPack.Is_WriteUserPipe) {
        if ((CmdPack.UserPipeTo = getUserPipeToWho(v)) == 0) {
            #if _DEBUG_ == 1
            cerr << "Error : getUserPipeTo Who error";
            #endif
        }
    }

    if (CmdPack.Is_FileReDirection) {
        CmdPack.writePath = v[v.size() - 1];
        v.pop_back();
        v.pop_back();
    }
    if (CmdPack.Is_ReadUserPipe) {
        v.pop_back();
    }
    if (CmdPack.Is_WriteUserPipe) {
        v.pop_back();
    }

    CmdPack.param = v;

    for (const auto &c : CmdPack.param) CmdPack.CmdStr = CmdPack.CmdStr + " " + c;
    return CmdPack;
}

void childProcess(CMDtype OneCmdPack, int CmdNumber, int numberOfGeneralCmds, int PipeIn, int CmdPipeList[], bool writeSucess) {
    int fd;

    // set exec stdIN
    if (CmdNumber != 0) {
        dup2(CmdPipeList[(CmdNumber - 1) * 2], STDIN_FILENO);
    }
    else {
        if (PipeIn == -2)
            dup2(STDIN_FILENO, STDIN_FILENO);
        else
            dup2(PipeIn, STDIN_FILENO);
    }

    // set exec stdOUT
    if (OneCmdPack.Is_NumPipeCmd) {
        dup2(PIPEMAP[timestamp + OneCmdPack.PipeN].PipeFdNumber[1], STDOUT_FILENO);
        if (OneCmdPack.Is_StdError) 
            dup2(PIPEMAP[timestamp + OneCmdPack.PipeN].PipeFdNumber[1], STDERR_FILENO);
    }
    else if (OneCmdPack.Is_FileReDirection) {
        fd = open(OneCmdPack.writePath.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0777);
        dup2(fd, STDOUT_FILENO);
    }
    else if (CmdNumber < numberOfGeneralCmds - 1) {
        dup2(CmdPipeList[CmdNumber * 2 + 1], STDOUT_FILENO);
    }
    else if (OneCmdPack.Is_WriteUserPipe) {
        if (!writeSucess) {
            dup2(FD_NULL, STDOUT_FILENO);
        }
        else {
            int writeTo = OneCmdPack.UserPipeTo;
            char FIFOName[20];
            memset(FIFOName, '\0', sizeof(FIFOName));
            sprintf(FIFOName, "/tmp/userPipe-%d-%d", ClientID, writeTo);
            mkfifo(FIFOName, 0666);
            USERPIPEMAP[ClientID].PipeFdNumber[writeTo] = -2;
            kill(CLIENTMAP[writeTo].pid, SIGUSR2);
            int FIFOFd = open(FIFOName, O_WRONLY);
            if (FIFOFd < 0) {
                perror("open");
                dup2(FD_NULL, STDOUT_FILENO);
            }
            else {
                dup2(FIFOFd, STDOUT_FILENO);
                close(FIFOFd);
            }
            
        }
    }
    // else {
    //     dup2(STDOUT_FILENO, STDOUT_FILENO);
    // }

    // Close the Command Pipe List
    for (int i = 0; i < (numberOfGeneralCmds - 1) * 2; i++) {
        close(CmdPipeList[i]);
    }

    // Close Number Pipe
    for (auto numberpipe : PIPEMAP) {
        close(numberpipe.second.PipeFdNumber[0]);
        close(numberpipe.second.PipeFdNumber[1]);
    }

    // Close User Pipe
    for (int x = 1; x < MAX_CLIENT; x++) {
        for (int y = 1; y < MAX_CLIENT; y++) {
            close(USERPIPEMAP[x].PipeFdNumber[y]);
        }
    }
    
    // Execute the Command
    vector<string> bufVec = OneCmdPack.param;
    bufVec.insert(bufVec.begin(), OneCmdPack.BIN);
    char **argv = vectorToCharPointer(bufVec);
    int e = execvp(argv[0], argv);

    if(e == -1) {
        string test = vector2String(bufVec);
        cerr << "Unknown command: [" << OneCmdPack.BIN << "].\n";
    }
    exit(0);
}

void parentProcess(CMDtype OneCmdPack, int CmdNumber, int CmdPipeList[], int pipeTimes, string inputCmd) {
    waitpid(-1, &status, WNOHANG);
    
    if (CmdNumber > 0) {
        close(CmdPipeList[(CmdNumber - 1) * 2]);
        close(CmdPipeList[(CmdNumber - 1) * 2 + 1]);
    }
    
    // close and wait numbered pipe
    if (PIPEMAP.find(timestamp) != PIPEMAP.end()) {
        // for (pid_t pid : PIPEMAP[timestamp].PipeFromPids)
        //     waitpid(pid, NULL, 0);
        close(PIPEMAP[timestamp].PipeFdNumber[0]);
        close(PIPEMAP[timestamp].PipeFdNumber[1]);
        PIPEMAP.erase(timestamp);
    }

    if (OneCmdPack.Is_ReadUserPipe) {
        char FIFOName[20];
        memset(FIFOName, '\0', sizeof(FIFOName));
        sprintf(FIFOName, "/tmp/userPipe-%d-%d", OneCmdPack.UserPipeFrom, ClientID);
        unlink(FIFOName);
    }
}

// return > 0 means current command is a legal command, else id illegal
int CmdProcess(vector<string> cmdSplit, string inputCmd) {
    size_t findPipe;
    int pipeTimes;
    pid_t pid;
    vector<string> cmdVec = splitLine2EachCmd(cmdSplit);

    // here to get per command => ex. if command : ls |1 number | cat => 1. (ls |1 ) 2. (number | cat )
    for (int i = 0; i < cmdVec.size(); i++) {
        string currentCmd = cmdVec[i];
        
        // here to get normal command (split general pipe) ex. if command : ls | cat => 1. (ls |) 2. (cat)
        vector<string> GeneralOrNonOrNumPipeCmds = splitEachCmd2GeneralOrNonOrNumPipeCmds(currentCmd);
        int numberOfGeneralCmds = GeneralOrNonOrNumPipeCmds.size();

        int CmdPipeList[(numberOfGeneralCmds - 1) * 2];
        memset(CmdPipeList, -1, sizeof(CmdPipeList));

        // First Need to check whether exist stdIN, get the stdIn
        int PipeIn = -2;
        if (PIPEMAP.find(timestamp) != PIPEMAP.end()) {
            PipeIn = PIPEMAP[timestamp].PipeFdNumber[0];
        }
        
        for (int y = 0; y < numberOfGeneralCmds; ++y) {
            pipeTimes = 0;

            string OneCmd = GeneralOrNonOrNumPipeCmds[y];
            CMDtype OneCmdPack = pareseOneCmd(OneCmd);
            // cout << "PipeFromWho: " << OneCmdPack.UserPipeFrom << " PipeToWho: " <<  OneCmdPack.UserPipeTo << endl;
            
            // Build Pipe
            // If current is the NumPipeCmd
            if (OneCmdPack.Is_NumPipeCmd) {
                pipeTimes = OneCmdPack.PipeN;
                if (PIPEMAP.find(timestamp + pipeTimes) == PIPEMAP.end())
                    pipe(PIPEMAP[timestamp + pipeTimes].PipeFdNumber);
            }
            // else if pre is the PipeCmd, Make a pipe
            else if (y != numberOfGeneralCmds - 1) {
                pipe(CmdPipeList + y * 2);
            }

            if (OneCmdPack.Is_ReadUserPipe && PipeIn == -2) {
                int readFrom = OneCmdPack.UserPipeFrom;
                // Case : sender client does not exist yet, output *** Error: user #<sender_id> does not exist yet. ***
                if (!CLIENTMAP[readFrom].alive) {
                    string msg = "*** Error: user #" + to_string(readFrom) + " does not exist yet. ***\n";
                    cout << msg;
                    PipeIn = FD_NULL;
                }
                // Case : pipe does not exist, output *** Error: the pipe #<sender_id>->#<receiver_id> does not exist yet. ***
                else if (USERPIPEMAP[readFrom].PipeFdNumber[ClientID] < 0) {
                    string msg = "*** Error: the pipe #" + to_string(readFrom) + "->#" + to_string(ClientID) + " does not exist yet. ***\n";
                    cout << msg;
                    PipeIn = FD_NULL;
                }
                else {
                    // Case : Read pipe sucessfully, output *** <receiver_name> (#<receiver_id>) just received from <sender_name> (#<sender_id>) by ’<command>’ ***
                    string bmsg = "*** " + string(CLIENTMAP[ClientID].username) + " (#" + to_string(ClientID) + ") just received from "\
                            + CLIENTMAP[readFrom].username + " (#" + to_string(readFrom) + ") by '" + inputCmd + "' ***\n";
                    BroadcastMessage(bmsg);
                    PipeIn = USERPIPEMAP[readFrom].PipeFdNumber[ClientID];
                    USERPIPEMAP[readFrom].PipeFdNumber[ClientID] = -1;
                }
            }

            // Process Write User Pipe Case : >{client N}
            bool writeSucess = true;
            if (OneCmdPack.Is_WriteUserPipe) {
                int writeTo = OneCmdPack.UserPipeTo;
                // Case : Receiver does not exist, output "*** Error: user #<user_id> does not exist yet. ***"
                if (!CLIENTMAP[writeTo].alive) {
                    string msg = "*** Error: user #" + to_string(writeTo) + " does not exist yet. ***\n";
                    cout << msg;
                    writeSucess = false;
                }
                // Case : User pipe already exists, output "*** Error: the pipe #<sender_id>->#<receiver_id> already exists. ***"
                else if (USERPIPEMAP[ClientID].PipeFdNumber[writeTo] > 0) {
                    string msg = "*** Error: the pipe #" + to_string(ClientID) + "->#" + to_string(writeTo) + " already exists. ***\n";
                    cout << msg;
                    writeSucess = false;
                }
                else {
                    // Case : Write pipe sucessfully, output *** <sender_name> (#<sender_id>) just piped ’<command>’ to <receiver_name> (#<receiver_id>) ***
                    string bmsg = "*** " + string(CLIENTMAP[ClientID].username) + " (#" + to_string(ClientID) + ") just piped '"\
                                + inputCmd + "' to " + CLIENTMAP[writeTo].username + " (#" + to_string(writeTo) + ") ***\n";
                    BroadcastMessage(bmsg);
                }
            } 

            while((pid = fork()) < 0) {
                waitpid(-1, NULL, 0);
            }
            // child exec Cmd here.
            if (pid == 0) {
                childProcess(OneCmdPack, y, numberOfGeneralCmds, PipeIn, CmdPipeList, writeSucess);
            }
            // parent wait for child done.
            else if (pid > 0) {
                parentProcess(OneCmdPack, y, CmdPipeList, pipeTimes, inputCmd);
                if (OneCmdPack.Is_NumPipeCmd) {
                    // PIPEMAP[timestamp + pipeTimes].PipeFromPids.push_back(pid);
                    usleep(1000*200);
                }
                if (!OneCmdPack.Is_NumPipeCmd) {
                    // cout << "parent: " << OneCmdPack.CmdStr << endl;
                    if (y == numberOfGeneralCmds - 1)
                        waitpid(pid, NULL, 0);
                }
            }
        }
        timestamp++;
    }
    return 1;
}

int runNpShell(char cmd[BUFSIZE]) {
    vector<string> cmdSplit;
    string retMessage = "";
    
    cmdSplit = splitLineSpace(cmd);
    if (cmdSplit.size() == 0)
        return 0;
    string inputCmd = vector2String(cmdSplit);
    string BIN = cmdSplit[0];
    string PATH = cmdSplit.size() >= 2 ? cmdSplit[1] : "";
    string third = cmdSplit.size() >= 3 ? cmdSplit[2] : "";

    if (BIN == "printenv") {
        if (PATH == "")
            return 0;
        if (getenv(PATH.c_str()) != NULL) {
            retMessage = getenv(PATH.c_str());
            retMessage += "\n";
            cout << retMessage;
        }
    }
    else if (BIN == "exit") {
        return -1;
    }
    else if (BIN == "setenv") {
        if (PATH == "" || third == "")
            return 0;
        setenv(PATH.c_str(), third.c_str(), 1);
    }
    else if (BIN == "who") {
        who(ClientID);
    }
    else if (BIN == "tell") {
        tell(ClientID, stoi(PATH) ,vector2String(cmdSplit, 2));
    }
    else if (BIN == "yell") {
        yell(ClientID, vector2String(cmdSplit, 1));
    }
    else if (BIN == "name") {
        if (PATH == "") return 0;
        name(ClientID, PATH);
    }
    else {
        if (CmdProcess(cmdSplit, inputCmd) < 0) {return 0;}
        timestamp--;
    }
    timestamp++;
    return 0;
}

// return minimum "can use" user id
int getCanUseUserID() {
    for (int i = 1; i < MAX_CLIENT; i++) {
        if (!CLIENTMAP[i].alive) return i;
    }
    return -1;
}

// process new client login
void NewClientComing(sockaddr_in sa, int clientID, pid_t pid, int ssock) {
    close(ssock);
    // extract new client ip
    memset(CLIENTMAP[clientID].IP, '\0', sizeof(CLIENTMAP[clientID].IP));
    inet_ntop(AF_INET, &(sa.sin_addr), CLIENTMAP[clientID].IP, INET_ADDRSTRLEN);
    // extract new client port
    CLIENTMAP[clientID].INPort = ntohs(sa.sin_port);
    CLIENTMAP[clientID].pid = pid;
    memset(CLIENTMAP[clientID].username, '\0', sizeof(CLIENTMAP[clientID].username));
    strcpy(CLIENTMAP[clientID].username, "(no name)");
    CLIENTMAP[clientID].alive = true;
}

void createSharedMemory() {
    if ((SHMClientInfoId = shmget(SHMKEY_ClientInfo, sizeof(Client) * MAX_CLIENT, 0666|IPC_CREAT)) < 0 ) {
        perror("shmget");
    }
    CLIENTMAP = (struct Client*)shmat(SHMClientInfoId, 0, 0);

    if ((SHMMsgId = shmget(SHMKEY_Msg, sizeof(char) * BUFSIZE, 0666|IPC_CREAT)) < 0) {
        perror("shmget");
    }
    CLIENTMsg = (char*)shmat(SHMMsgId, 0, 0);

    if ((SHMUserPipeId = shmget(SHMKEY_UserPipe, sizeof(UserPipeFd) * MAX_CLIENT, 0666|IPC_CREAT)) < 0) {
        perror("shmget");
    }
    USERPIPEMAP = (struct UserPipeFd*)shmat(SHMUserPipeId, 0, 0);
}

void initSharedMemory() {
    // initialized all client information
    for (int i = 0 ; i < MAX_CLIENT; i++) {
        CLIENTMAP[i].alive = false;
        CLIENTMAP[i].INPort = -1;
        CLIENTMAP[i].pid = -1;
        memset(CLIENTMAP[i].IP, '\0', sizeof(CLIENTMAP[i].IP));
        memset(CLIENTMAP[i].username, '\0', sizeof(CLIENTMAP[i].username));
        strcpy(CLIENTMAP[i].username, "(no name)");
    }

    // initialized all client USERPIPEMAP
    for (int x = 1; x < MAX_CLIENT; x++) {
        for (int y = 1; y < MAX_CLIENT; y++) {
            USERPIPEMAP[x].PipeFdNumber[y] = -1;
        }
    }

    // initialized shared Message
    memset(CLIENTMsg, '\0', sizeof(CLIENTMsg));
}

void initSignal() {
    signal(SIGINT, shudwn);
    signal(SIGUSR1, outputBMsg);
    signal(SIGUSR2, sigFifo);
    signal(SIGCHLD, signalHandler);
}

void init() {
    clearenv();
    setenv("PATH", "bin:.", 1);
    cout.setf(ios::unitbuf);
    timestamp = 0;
}

int main(int argc, char *argv[]) {
    int sock, ssock;
    struct sockaddr_in clientAddress,serv_addr;
    socklen_t clientAddressLength = sizeof(clientAddress);
    
    bool opt = true;
    socklen_t optlen = sizeof(bool);

    if (argc > 1) {
        string port = argv[1];
        if (stoi(port)) {
            SERV_TCP_PORT = stoi(port);
        }
    }
    if ( (sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        cerr << "server: can't open stream socket" << endl;
        return 0;
    }
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt, optlen);

    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(SERV_TCP_PORT);

    if (bind(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // the second argument is backlog defines the maximum length for the queue of pending connections.
    if (listen(sock, MAX_CLIENT) < 0) {
        perror("listen");
        exit(1);
    }

    createSharedMemory(); // build shared memory, and map to Client* CLIENTMAP , char* CLIENTMsg, UserPipeFd* USERPIPEMAP
    initSharedMemory();
    initSignal();

    pid_t pid;
    while(true) {
        // Means new client come in
        if ((ssock = accept(sock, (struct sockaddr *) &clientAddress, &clientAddressLength)) < 0) {
            perror("accept");
            continue;
        }
        int newClientID = getCanUseUserID();
        if (newClientID == -1) {
            cerr << "The current user limit has been reached" << endl;
            continue;
        }
        while ((pid = fork()) < 0) {
            waitpid(-1, &status, WNOHANG);
            continue;
        }
        if (pid > 0) { // parent process
            NewClientComing(clientAddress, newClientID, pid, ssock);
        }
        else if (pid == 0) {
            ClientID = newClientID;
            close(sock);
            dup2(ssock, STDIN_FILENO);
            dup2(ssock, STDOUT_FILENO);
            dup2(ssock, STDERR_FILENO);
            close(ssock);
            init();
            string helloMsg = "****************************************\n** Welcome to the information server. **\n****************************************\n";
            cout << helloMsg;
            while(!CLIENTMAP[ClientID].alive) usleep(1000);
            BroadcastMessage("*** User '" + string(CLIENTMAP[ClientID].username) + "' entered from " + CLIENTMAP[ClientID].IP + ":" + to_string(CLIENTMAP[newClientID].INPort) + ". ***\n");
            cout << "% ";
            int readstate;
            char message[BUFSIZE];
            while(true) {
                memset(message, 0, BUFSIZE);
                if ((readstate = read(0, message, BUFSIZE)) < 0) {
                    perror("read");
                    continue;
                }

                else if (readstate >= 0) {
                    if (readstate == 0 || runNpShell(message) == -1) {
                        CLIENTMAP[ClientID].alive = false;
                        string msg = "*** User '" + string(CLIENTMAP[ClientID].username) + "' left. ***\n";
                        BroadcastMessage(msg);
                        for (int i = 1; i < MAX_CLIENT; i++) {
                            USERPIPEMAP[i].PipeFdNumber[ClientID] = -1;
                        }
                        for (int i = 1; i < MAX_CLIENT; i++) {
                            USERPIPEMAP[ClientID].PipeFdNumber[i] = -1;
                        }
                        shmdt(CLIENTMAP);
                        shmdt(CLIENTMsg);
                        shmdt(USERPIPEMAP);
                        exit(0);
                    }
                    cout << "% ";
                }
            }
        }
       
    }
    return 0;
}