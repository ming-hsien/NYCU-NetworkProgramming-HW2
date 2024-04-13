#include <iostream>
#include <string.h>
#include <sstream>
#include <map>
#include <vector>
#include <queue>
#include <regex>

#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/socket.h>

// this is for open file for file descriptor
#include <fcntl.h>
// this is for construct socket
#include <arpa/inet.h>

using namespace std;

// #define PROCESS_LIMIT 512
#define _DEBUG_ 0
#define BUFSIZE 15000

int status;
int SERV_TCP_PORT = 7001;
const int FD_NULL = open("/dev/null", O_RDWR);

struct PipeFd {
    vector<pid_t> PipeFromPids;
    int PipeFdNumber[2] = {-1, -1};
};

struct UserPipeFd {
    int PipeFdNumber[2] = {-1, -1};
};

struct Client {
    char IP[INET_ADDRSTRLEN];
    int ID = -1;
    int INPort = -1;
    int SOCK = -1;
    map<string, string> envs;
    string username = "(no name)";
    map<int, PipeFd> PIPEMAP;
    map<int, UserPipeFd> USERPIPEMAP; // key = who want to send me message, value = pipe to read message.
    int timestamp = 0;
};

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

// key is client user id
map<int, Client> CLIENTMAP;
int numClient = 0;

void init(int);
int getIDFromSock(int);

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

void BroadcastMessage(string msg) {
    for (auto cmap : CLIENTMAP) {
        if (write(cmap.second.SOCK, msg.c_str(), sizeof(char) * msg.length()) < 0) {
            perror("write");
        }
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

void childProcess(int clientID, CMDtype OneCmdPack, int CmdNumber, int numberOfGeneralCmds, int PipeIn, int CmdPipeList[], bool writeSucess) {
    int fd;
    int STDBUF = STDOUT_FILENO;

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
    dup2(CLIENTMAP[clientID].SOCK, STDERR_FILENO);

    // set exec stdOUT
    if (OneCmdPack.Is_NumPipeCmd) {
        dup2(CLIENTMAP[clientID].PIPEMAP[CLIENTMAP[clientID].timestamp + OneCmdPack.PipeN].PipeFdNumber[1], STDOUT_FILENO);
        if (OneCmdPack.Is_StdError) 
            dup2(CLIENTMAP[clientID].PIPEMAP[CLIENTMAP[clientID].timestamp + OneCmdPack.PipeN].PipeFdNumber[1], STDERR_FILENO);
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
            dup2(CLIENTMAP[writeTo].USERPIPEMAP[clientID].PipeFdNumber[1], STDOUT_FILENO);
        }
    }
    else {
        dup2(CLIENTMAP[clientID].SOCK, STDOUT_FILENO);
    }

    // Close the Command Pipe List
    for (int i = 0; i < (numberOfGeneralCmds - 1) * 2; i++) {
        close(CmdPipeList[i]);
    }

    // Close Number Pipe
    for (auto numberpipe : CLIENTMAP[clientID].PIPEMAP) {
        close(numberpipe.second.PipeFdNumber[0]);
        close(numberpipe.second.PipeFdNumber[1]);
    }

    // Close User Pipe
    for (auto userpipe : CLIENTMAP[clientID].USERPIPEMAP) {
        close(userpipe.second.PipeFdNumber[0]);
        close(userpipe.second.PipeFdNumber[1]);
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

void parentProcess(int clientID, CMDtype OneCmdPack, int CmdNumber, int CmdPipeList[], int pipeTimes, string inputCmd) {
    waitpid(-1, &status, WNOHANG);
    
    if (CmdNumber > 0) {
        close(CmdPipeList[(CmdNumber - 1) * 2]);
        close(CmdPipeList[(CmdNumber - 1) * 2 + 1]);
    }
    
    // close and wait numbered pipe
    if (CLIENTMAP[clientID].PIPEMAP.find(CLIENTMAP[clientID].timestamp) != CLIENTMAP[clientID].PIPEMAP.end()) {
        // for (pid_t pid : PIPEMAP[timestamp].PipeFromPids)
        //     waitpid(pid, NULL, 0);
        close(CLIENTMAP[clientID].PIPEMAP[CLIENTMAP[clientID].timestamp].PipeFdNumber[0]);
        close(CLIENTMAP[clientID].PIPEMAP[CLIENTMAP[clientID].timestamp].PipeFdNumber[1]);
        CLIENTMAP[clientID].PIPEMAP.erase(CLIENTMAP[clientID].timestamp);
    }

    if (OneCmdPack.Is_ReadUserPipe) {
        close(CLIENTMAP[clientID].USERPIPEMAP[OneCmdPack.UserPipeFrom].PipeFdNumber[0]);
        close(CLIENTMAP[clientID].USERPIPEMAP[OneCmdPack.UserPipeFrom].PipeFdNumber[1]);
        CLIENTMAP[clientID].USERPIPEMAP.erase(OneCmdPack.UserPipeFrom);
    }
}

// Process who command
void who(int ID) {
    string msg = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
    for (auto cmap : CLIENTMAP) {
        if (cmap.first == ID) {
            msg = msg + to_string(cmap.second.ID) + '\t' + cmap.second.username + '\t' + cmap.second.IP + ":" + to_string(cmap.second.INPort) + "\t<-me\n";
        }
        else {
            msg = msg + to_string(cmap.second.ID) + '\t' + cmap.second.username + '\t' + cmap.second.IP + ":" + to_string(cmap.second.INPort) + "\n";
        }
    }
    write(CLIENTMAP[ID].SOCK, msg.c_str(), sizeof(char) * msg.length());
}

// Process tell command
void tell(int fromID, int toWhoID, string msg) {
    // Not Found specific client
    if (CLIENTMAP.find(toWhoID) == CLIENTMAP.end()) {
        string NotFoundMsg = "*** Error: user #" + to_string(toWhoID) + " does not exist yet. ***\n";
        write(CLIENTMAP[fromID].SOCK, NotFoundMsg.c_str(), sizeof(char) * NotFoundMsg.length());
    }
    // Find and send msg to specific client
    else {
        string retMsg = "*** " + CLIENTMAP[fromID].username + " told you ***: " + msg + "\n";
        write(CLIENTMAP[toWhoID].SOCK, retMsg.c_str(), sizeof(char) * retMsg.length());
    }
}

// Process yell command
void yell(int fromID, string msg) {
    string yellMsg = "*** " + CLIENTMAP[fromID].username + " yelled ***: " + msg + "\n";
    BroadcastMessage(yellMsg);
}

// Process name command
void name(int userID, string newname) {
    // Can not have two client have same user name.
    bool repeat = false;
    for (auto cmap : CLIENTMAP) {
        if (cmap.second.username == newname) {
            repeat = true;
            break;
        }
    }
    if (repeat) {
        string msg = "*** User '" + newname + "' already exists. ***\n";
        write(CLIENTMAP[userID].SOCK, msg.c_str(), sizeof(char) * msg.length());
    }
    else {
        string IP_ = CLIENTMAP[userID].IP;
        CLIENTMAP[userID].username = newname;
        string broadcastMsg = "*** User from " + IP_ + ":" + to_string(CLIENTMAP[userID].INPort) + " is named '" + newname + "'. ***\n";
        BroadcastMessage(broadcastMsg);
    }
}

// return > 0 means current command is a legal command, else id illegal
int CmdProcess(int clientID, vector<string> cmdSplit, string inputCmd) {
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
        if (CLIENTMAP[clientID].PIPEMAP.find(CLIENTMAP[clientID].timestamp) != CLIENTMAP[clientID].PIPEMAP.end()) {
            PipeIn = CLIENTMAP[clientID].PIPEMAP[CLIENTMAP[clientID].timestamp].PipeFdNumber[0];
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
                if (CLIENTMAP[clientID].PIPEMAP.find(CLIENTMAP[clientID].timestamp + pipeTimes) == CLIENTMAP[clientID].PIPEMAP.end())
                    pipe(CLIENTMAP[clientID].PIPEMAP[CLIENTMAP[clientID].timestamp + pipeTimes].PipeFdNumber);
            }
            // else if pre is the PipeCmd, Make a pipe
            else if (y != numberOfGeneralCmds - 1) {
                pipe(CmdPipeList + y * 2);
            }

            if (OneCmdPack.Is_ReadUserPipe && PipeIn == -2) {
                int readFrom = OneCmdPack.UserPipeFrom;
                // Case : sender client does not exist yet, output *** Error: user #<sender_id> does not exist yet. ***
                if (CLIENTMAP.find(readFrom) == CLIENTMAP.end()) {
                    string msg = "*** Error: user #" + to_string(readFrom) + " does not exist yet. ***\n";
                    write(CLIENTMAP[clientID].SOCK, msg.c_str(), sizeof(char) * msg.length());
                    PipeIn = FD_NULL;
                }
                // Case : pipe does not exist, output *** Error: the pipe #<sender_id>->#<receiver_id> does not exist yet. ***
                else if (CLIENTMAP[clientID].USERPIPEMAP.find(readFrom) == CLIENTMAP[clientID].USERPIPEMAP.end()) {
                    string msg = "*** Error: the pipe #" + to_string(readFrom) + "->#" + to_string(clientID) + " does not exist yet. ***\n";
                    write(CLIENTMAP[clientID].SOCK, msg.c_str(), sizeof(char) * msg.length());
                    PipeIn = FD_NULL;
                }
                else {
                    // Case : Read pipe sucessfully, output *** <receiver_name> (#<receiver_id>) just received from <sender_name> (#<sender_id>) by ’<command>’ ***
                    string bmsg = "*** " + CLIENTMAP[clientID].username + " (#" + to_string(clientID) + ") just received from "\
                            + CLIENTMAP[OneCmdPack.UserPipeFrom].username + " (#" + to_string(OneCmdPack.UserPipeFrom) + ") by '" + inputCmd + "' ***\n";
                    BroadcastMessage(bmsg);
                    PipeIn = CLIENTMAP[clientID].USERPIPEMAP[readFrom].PipeFdNumber[0];
                }
            }

            // Process Write User Pipe Case : >{client N}
            bool writeSucess = true;
            if (OneCmdPack.Is_WriteUserPipe) {
                int writeTo = OneCmdPack.UserPipeTo;
                // Case : Receiver does not exist, output "*** Error: user #<user_id> does not exist yet. ***"
                if (CLIENTMAP.find(writeTo) == CLIENTMAP.end()) {
                    string msg = "*** Error: user #" + to_string(writeTo) + " does not exist yet. ***\n";
                    write(CLIENTMAP[clientID].SOCK, msg.c_str(), sizeof(char) * msg.length());
                    writeSucess = false;
                }
                // Case : User pipe already exists, output "*** Error: the pipe #<sender_id>->#<receiver_id> already exists. ***"
                else if (CLIENTMAP[writeTo].USERPIPEMAP.find(clientID) != CLIENTMAP[writeTo].USERPIPEMAP.end()) {
                    string msg = "*** Error: the pipe #" + to_string(clientID) + "->#" + to_string(writeTo) + " already exists. ***\n";
                    write(CLIENTMAP[clientID].SOCK, msg.c_str(), sizeof(char) * msg.length());
                    writeSucess = false;
                }
                else {
                    // Case : Write pipe sucessfully, output *** <sender_name> (#<sender_id>) just piped ’<command>’ to <receiver_name> (#<receiver_id>) ***
                    if (OneCmdPack.Is_WriteUserPipe) {
                        string bmsg = "*** " + CLIENTMAP[clientID].username + " (#" + to_string(clientID) + ") just piped '"\
                                + inputCmd + "' to " + CLIENTMAP[OneCmdPack.UserPipeTo].username + " (#" + to_string(OneCmdPack.UserPipeTo) + ") ***\n";
                        BroadcastMessage(bmsg);
                    }
                    pipe(CLIENTMAP[writeTo].USERPIPEMAP[clientID].PipeFdNumber);
                }
            }

            while((pid = fork()) < 0) {
                waitpid(-1, NULL, 0);
            }
            // child exec Cmd here.
            if (pid == 0) {
                childProcess(clientID, OneCmdPack, y, numberOfGeneralCmds, PipeIn, CmdPipeList, writeSucess);
            }
            // parent wait for child done.
            else if (pid > 0) {
                parentProcess(clientID, OneCmdPack, y, CmdPipeList, pipeTimes, inputCmd);
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
        CLIENTMAP[clientID].timestamp++;
    }
    return 1;
}

int runNpShell(int clientfd, char cmd[BUFSIZE]) {
    vector<string> cmdSplit;
    string retMessage = "";
    
    int clientID = getIDFromSock(clientfd);
    init(clientID);
    
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
            write(clientfd, retMessage.c_str(), sizeof(char) * retMessage.length());
        }
    }
    else if (BIN == "exit") {
        return -1;
    }
    else if (BIN == "setenv") {
        if (PATH == "" || third == "")
            return 0;
        setenv(PATH.c_str(), third.c_str(), 1);
        CLIENTMAP[clientID].envs[PATH] = third;
    }
    else if (BIN == "who") {
        who(clientID);
    }
    else if (BIN == "tell") {
        tell(clientID, stoi(PATH) ,vector2String(cmdSplit, 2));
    }
    else if (BIN == "yell") {
        yell(clientID, vector2String(cmdSplit, 1));
    }
    else if (BIN == "name") {
        if (PATH == "") return 0;
        name(clientID, PATH);
    }
    else {
        if (CmdProcess(clientID, cmdSplit, inputCmd) < 0) {return 0;}
        CLIENTMAP[clientID].timestamp--;
    }
    CLIENTMAP[clientID].timestamp++;
    return 0;
}

// input sock return client id
int getIDFromSock(int sock) {
    for (auto clientmap : CLIENTMAP) {
        if (clientmap.second.SOCK == sock) return clientmap.second.ID;
    }
    return -1;
}

// return minimum "can use" user id
int getCanUseUserID() {
    int minid = 1;
    for (auto clientmap : CLIENTMAP) {
        if (clientmap.first != minid) {
            return minid;
        }
        else {
            minid++;
        }
    }
    return minid;
}

// process new client login and return new client ID
int NewClientComing(sockaddr_in sa, int sock) {
    Client newClient;
    // extract new client ip
    inet_ntop(AF_INET, &(sa.sin_addr), newClient.IP, INET_ADDRSTRLEN);
    // extract new client port
    newClient.INPort = ntohs(sa.sin_port);
    //  assign user id
    newClient.ID = getCanUseUserID();
    // set base env path
    newClient.envs["PATH"] = "bin:.";
    // put new client into client table
    newClient.SOCK = sock;
    // add to client map
    CLIENTMAP[newClient.ID] = newClient;
    
    // print hello message
    string helloMessage = "****************************************\n** Welcome to the information server. **\n****************************************\n";
    if (write(sock, helloMessage.c_str(), sizeof(char) * helloMessage.length()) < 0) {
        perror("write");
    }
    string comingMsg = "*** User '" + newClient.username + "' entered from " + newClient.IP + ":" + to_string(newClient.INPort) + ". ***\n";
    BroadcastMessage(comingMsg);
    write(sock, "% ", 2);

    #if _DEBUG_ == 1
    cout << newClient.ID << " " << newClient.IP << ":" << newClient.INPort<< endl;
    #endif

    return newClient.ID;
}

void init(int ID) {
    clearenv();
    cout.setf(ios::unitbuf);
    for (auto env : CLIENTMAP[ID].envs) {
        setenv(env.first.c_str(), env.second.c_str(), 1);
    }
}

int main(int argc, char *argv[]) {
    // master sock & slave sock
    int msock, ssock;

    // server address info
    struct sockaddr_in serverAddress;

    // client address info.
    struct sockaddr_in clientAddress;
    socklen_t clientAddressLength;

    // rfds -> record which sock not enmpty
    // afds -> record which sock is online
    fd_set rfds;
    fd_set afds;

    // nfds -> table size of rfds & afds
    int fd, nfds;
    
    bool opt = true;
    socklen_t optlen = sizeof(bool);
    
    // open service port
    if (argc > 1) {
        string port = argv[1];
        if (stoi(port)) {
            SERV_TCP_PORT = stoi(port);
        }
    }
    if ( (msock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        #if _DEBUG_ == 1
        cerr << "server: can't open stream socket" << endl;
        #endif
        return 0;
    }
    setsockopt(msock,SOL_SOCKET,SO_REUSEADDR|SO_REUSEPORT,&opt,optlen);

    bzero(&serverAddress, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(SERV_TCP_PORT);

    if (bind(msock, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0) {
        perror("bind");
        exit(1);
    }

    // the second argument is backlog defines the maximum length for the queue of pending connections.
    if (listen(msock, 30) < 0) {
        perror("listen");
        exit(1);
    }

    // initial FDs
    nfds = FD_SETSIZE;
    FD_ZERO(&afds);
    FD_SET(msock, &afds);
    clientAddressLength = sizeof(clientAddress);

    while(true) {
        memcpy(&rfds, &afds, sizeof(rfds));
        if (select(nfds, &rfds, (fd_set*)0, (fd_set*)0, (struct timeval*)0) < 0) {
            perror("select");
            continue;
        }

        // Master sock (msock) detect if New client coming
        if (FD_ISSET(msock, &rfds)) {
            ssock = accept(msock, (struct sockaddr *) &clientAddress, &clientAddressLength);
            if (ssock < 0) perror("accept");
            int newClientID = NewClientComing(clientAddress, ssock);
            // Record new client to afds
            FD_SET(ssock, &afds);
            numClient++;
        }
        
        // start serve each client for who have send message
        for (fd = 0; fd < nfds; fd++) {
            if (fd != msock && FD_ISSET(fd, &rfds)) {
                char message[BUFSIZE];
                memset(message, 0, BUFSIZE);
                // read message & process request
                int readstate;
                if ((readstate = read(fd, message, BUFSIZE)) < 0) {
                    perror("read");
                    continue;
                }
                // read state == 0 -> client send disconnect message
                else if (readstate == 0) {
                    CLIENTMAP.erase(getIDFromSock(fd));
                    (void)close(fd);
                    FD_CLR(fd, &afds);
                    numClient--;
                    continue;
                }
                else {
                    if (runNpShell(fd, message) == -1) {
                        // Somebody exit
                        int ID = getIDFromSock(fd);
                        string msg = "*** User '" + CLIENTMAP[ID].username + "' left. ***\n";
                        BroadcastMessage(msg);
                        for (auto cmap : CLIENTMAP) {
                            if (cmap.second.USERPIPEMAP.find(ID) != cmap.second.USERPIPEMAP.end()) {
                                cmap.second.USERPIPEMAP.erase(ID);
                            }
                        }
                        CLIENTMAP.erase(ID);
                        (void)close(fd);
                        FD_CLR(fd, &afds);
                        numClient--;
                        continue;
                    }
                    write(fd, "% ", 2);
                }
            }
        }
    }
    return 0;
}