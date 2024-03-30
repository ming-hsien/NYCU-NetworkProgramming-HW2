#include <iostream>
#include <string.h>
#include <sstream>
#include <map>
#include <vector>
#include <queue>

#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

// this is for open file for file descriptor
#include <fcntl.h>
// this is for construct socket
#include <arpa/inet.h>

using namespace std;

#define PROCESS_LIMIT 512
#define _DEBUG_ 0
#define SERV_TCP_PORT 7002
#define MAXLINE 512

int timestamp;
int status;

struct PipeFd {
    vector<pid_t> PipeFromPids;
    int PipeFdNumber[2] = {-1, -1};
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
};
map<int, PipeFd> PIPEMAP;

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
    if (Cmd.find('>') != string::npos)
        return true;
    return false;
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

char **stringToCharPointer(vector<string> &src) {
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

// return a type of CMD structure
CMDtype pareseOneCmd(string Cmd) {
    CMDtype CmdPack;
    CmdPack.Is_StdError = isStdError(Cmd);
    CmdPack.Is_PipeCmd = isPipeCmd(Cmd);
    CmdPack.Is_FileReDirection = isFileRedirectCmd(Cmd);
    CmdPack.Is_NumPipeCmd = isNumPipeCmd(Cmd);
    
    vector<string> v = splitLineSpace(Cmd);
    CmdPack.BIN = v[0];
    v.erase(v.begin());
    if (v.size() > 0 && CmdPack.Is_PipeCmd) {
        CmdPack.PipeN = getPipeTimes(Cmd);
        v.pop_back();
    }
    CmdPack.CmdStr += CmdPack.BIN;
    if (CmdPack.Is_FileReDirection) {
        CmdPack.writePath = v[v.size() - 1];
        v.pop_back();
        v.pop_back();
        CmdPack.param = v;
    }
    else 
        CmdPack.param = v;

    for (const auto &c : CmdPack.param) CmdPack.CmdStr = CmdPack.CmdStr + " " + c;
    return CmdPack;
}

void childProcess(CMDtype OneCmdPack, int CmdNumber, int numberOfGeneralCmds, int PipeIn, int CmdPipeList[]) {
    int fd;

    // set exec stdIN
    if (CmdNumber != 0) {
        dup2(CmdPipeList[(CmdNumber - 1) * 2], STDIN_FILENO);
    }
    else {
        if (PipeIn == -1)
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
    else {
        dup2(STDOUT_FILENO, STDOUT_FILENO);
    }

    // Close the Command Pipe List
    for (int i = 0; i < (numberOfGeneralCmds - 1) * 2; i++) {
        close(CmdPipeList[i]);
    }

    // Close Number Pipe
    for (auto numberpipe : PIPEMAP) {
        close(numberpipe.second.PipeFdNumber[0]);
        close(numberpipe.second.PipeFdNumber[1]);
    }

    // Execute the Command
    vector<string> bufVec = OneCmdPack.param;
    bufVec.insert(bufVec.begin(), OneCmdPack.BIN);
    char **argv = stringToCharPointer(bufVec);
    int e = execvp(argv[0], argv);

    if(e == -1)
        cerr << "Unknown command: [" << OneCmdPack.BIN << "].\n";
    exit(0);
}

void parentProcess(CMDtype OneCmdPack, int CmdNumber, int CmdPipeList[], int pipeTimes) {
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
}

void CmdProcess(vector<string> cmdSplit) {
    size_t findPipe;
    int pipeTimes;
    pid_t pid;
    vector<string> cmdVec = splitLine2EachCmd(cmdSplit);
    for (int i = 0; i < cmdVec.size(); i++) {
        string currentCmd = cmdVec[i];
        
        vector<string> GeneralOrNonOrNumPipeCmds = splitEachCmd2GeneralOrNonOrNumPipeCmds(currentCmd);
        int numberOfGeneralCmds = GeneralOrNonOrNumPipeCmds.size();

        int CmdPipeList[(numberOfGeneralCmds - 1) * 2];
        memset(CmdPipeList, -1, sizeof(CmdPipeList));

        // First Need to check whether exist stdIN, get the stdIn
        int PipeIn = -1;
        if (PIPEMAP.find(timestamp) != PIPEMAP.end()) {
            PipeIn = PIPEMAP[timestamp].PipeFdNumber[0];
        }
        
        for (int y = 0; y < numberOfGeneralCmds; ++y) {
            pipeTimes = 0;

            string OneCmd = GeneralOrNonOrNumPipeCmds[y];
            CMDtype OneCmdPack = pareseOneCmd(OneCmd);

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

            while((pid = fork()) < 0) {
                waitpid(-1, NULL, 0);
            }
            // child exec Cmd here.
            if (pid == 0) {
                childProcess(OneCmdPack, y, numberOfGeneralCmds, PipeIn, CmdPipeList);
            }
            // parent wait for child done.
            else if (pid > 0) {
                parentProcess(OneCmdPack, y, CmdPipeList, pipeTimes);
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
    return;
}

void runNpShell() {
    char cmd[MAXLINE];
    vector<string> cmdSplit;
    init();
    int n;
    while (true) {
        n = read(0, cmd, MAXLINE);
        if (n <= 0) break;
        cmdSplit = splitLineSpace(cmd);
        if (cmdSplit.size() == 0)
            continue;

        string BIN = cmdSplit[0];
        string PATH = cmdSplit.size() >= 2 ? cmdSplit[1] : "";
        string third = cmdSplit.size() >= 3 ? cmdSplit[2] : "";
        if (BIN == "printenv") {
            if (PATH == "")
                continue;
            if (getenv(PATH.c_str()) != NULL)
                cout << getenv(PATH.c_str()) << endl;
        }
        else if (BIN == "exit") {
            exit(0);
        }
        else if (BIN == "setenv") {
            if (PATH == "" || third == "")
                continue;
            setenv(PATH.c_str(), third.c_str(), 1);
        }
        else {
            CmdProcess(cmdSplit);
            timestamp--;
        }
        std::cout << "% ";
        timestamp++;
    }
}


void init() {
    clearenv();
    setenv("PATH", "bin:.", 1);
    timestamp = 0;
}

int main(int argc, char *argv[]) {

    int sockfd, newsockfd, clilen, childpid;
    struct sockaddr_in cli_addr,serv_addr;

    int val = 1, len = sizeof(val), n;

    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        cerr << "server: can't open stream socket" << endl;
        return 0;
    }

    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(SERV_TCP_PORT);
    
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    // the second argument is backlog defines the maximum length for the queue of pending connections.
    if (listen(sockfd, 3) < 0) {
        perror("listen");
        exit(1);
    }

    for ( ; ; ) {
        clilen = sizeof(cli_addr);
        dup2(0, 4);
		dup2(1, 5);
		dup2(2, 6);
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, (socklen_t *) &clilen);
        if (newsockfd < 0) cerr << "server: accept error" << endl;
        // close(sockfd);
        dup2(newsockfd, STDIN_FILENO);
        dup2(newsockfd, STDOUT_FILENO);
        // dup2(newsockfd, STDERR_FILENO);
        runNpShell();
        dup2(4, 0);
		dup2(5, 1);
		dup2(6, 2);
        close(newsockfd); /* parent process */
    }
    return 0;
}