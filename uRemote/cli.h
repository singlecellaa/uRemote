#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <pty.h>
#include <termios.h>
#endif

enum class ProcessState {
    NotStarted,
    Running,
    Stopped,
    Error
};

struct ProcessOutput {
    std::string text;
    bool isError;
};

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    bool start(const std::string& command = "");

    bool sendCommand(const std::string& command);

    std::vector<ProcessOutput> getOutput();

    bool isRunning() const;

    void stop();

    void setOutputCallback();

    void setOutputCallback(std::function<void(const std::string&, bool)> callback);

    ProcessState getState() const;

    bool busy() const;

private:
#ifdef _WIN32
    HANDLE hChildStdinRd = NULL;
    HANDLE hChildStdinWr = NULL;
    HANDLE hChildStdoutRd = NULL;
    HANDLE hChildStdoutWr = NULL;
    HANDLE hChildStderrRd = NULL;
    HANDLE hChildStderrWr = NULL;
    HANDLE hProcess = NULL;
#else
    int master_fd = -1;
    int slave_fd = -1;
    pid_t pid = -1;
#endif

    std::thread readThread;
    std::thread errorThread;
    std::mutex outputMutex;
    std::queue<ProcessOutput> outputQueue;
    std::atomic<ProcessState> state{ ProcessState::NotStarted };
    std::atomic<bool> shouldStop{ false };
    std::function<void(const std::string&, bool)> outputCallback;
    const std::string endMarker = "__PROCESS_MANAGER_EOF__";
    bool expectingCompletion = false;

    void checkMarker(std::string& output);
    void readOutput();
    void readError();
    void cleanup();
};