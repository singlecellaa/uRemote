#include "cli.h"
#include <iostream>
#include <array>
#include <chrono>

ProcessManager::ProcessManager() = default;

ProcessManager::~ProcessManager() {
    stop();
    cleanup();
}

#ifdef _WIN32
bool ProcessManager::start(const std::string& command) {
    if (state == ProcessState::Running) {
        return false;
    }

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Create pipes for stdin, stdout, stderr
    if (!CreatePipe(&hChildStdinRd, &hChildStdinWr, &saAttr, 0)) return false;
    if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0)) return false;
    if (!CreatePipe(&hChildStderrRd, &hChildStderrWr, &saAttr, 0)) return false;

    // Ensure the read handles are not inherited
    SetHandleInformation(hChildStdinWr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStderrRd, HANDLE_FLAG_INHERIT, 0);

    // Create the process
    PROCESS_INFORMATION piProcInfo;
    STARTUPINFO siStartInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));

    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = hChildStderrWr;
    siStartInfo.hStdOutput = hChildStdoutWr;
    siStartInfo.hStdInput = hChildStdinRd;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    std::string cmdLine = "cmd.exe";
    if (!command.empty()) {
        cmdLine += " /K " + command;
    }

    BOOL bSuccess = CreateProcess(
        NULL,
        const_cast<char*>(cmdLine.c_str()),
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &siStartInfo,
        &piProcInfo
    );

    if (!bSuccess) {
        cleanup();
        return false;
    }

    hProcess = piProcInfo.hProcess;
    CloseHandle(piProcInfo.hThread);

    // Start reader threads
    state = ProcessState::Running;
    shouldStop = false;
    readThread = std::thread(&ProcessManager::readOutput, this);
    errorThread = std::thread(&ProcessManager::readError, this);

    return true;
}

void ProcessManager::checkMarker(std::string& output) {
    if (expectingCompletion) {
        size_t markerPos1 = output.find(" & echo " + endMarker);
        if (markerPos1 != std::string::npos)
            output.erase(markerPos1, (" & echo " + endMarker).length());
        size_t markerPos2 = output.find(endMarker);
        if (markerPos2 != std::string::npos) {
            size_t markerPos2End = markerPos2 + endMarker.length();
            if (output[markerPos2End] == '\r' && output[markerPos2End + 1] == '\n')
                output.erase(markerPos2, endMarker.length() + 2);
            else
                output.erase(markerPos2, endMarker.length());
            expectingCompletion = false;
        }
    }
}

void ProcessManager::readOutput() {
    constexpr size_t bufferSize = 4096;
    std::array<char, bufferSize> buffer;

    while (!shouldStop) {
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(hChildStdoutRd, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            DWORD bytesRead;
            DWORD bytesToRead = (bytesAvailable < bufferSize - 1) ? bytesAvailable : bufferSize - 1;
            if (ReadFile(hChildStdoutRd, buffer.data(), bytesToRead, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string output(buffer.data());

                checkMarker(output);

                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    if (output.length())
                        outputQueue.push({ output, false });
                    if (expectingCompletion == false)
						pushSignal(SignalType::CMD_IDLE);
                    std::cout << "cmd push output: " << output << std::endl;
                }

                if (outputCallback) {
                    outputCallback(output, false);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Avoid busy waiting
    }
}

void ProcessManager::readError() {
    constexpr size_t bufferSize = 4096;
    std::array<char, bufferSize> buffer;

    while (!shouldStop) {
        DWORD bytesAvailable = 0;
        if (PeekNamedPipe(hChildStderrRd, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0) {
            DWORD bytesRead;
            DWORD bytesToRead = (bytesAvailable < bufferSize - 1) ? bytesAvailable : bufferSize - 1;
            if (ReadFile(hChildStderrRd, buffer.data(), bytesToRead, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string output(buffer.data());

                checkMarker(output);

                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    if (output.length()) {
                        outputQueue.push({ output, false });
                        std::cout << "cmd push output" << output << "to queue" << std::endl;
                    }
                }

                if (outputCallback) {
                    outputCallback(output, false);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Avoid busy waiting
    }
}

bool ProcessManager::sendCommand(const std::string& command) {
    if (state != ProcessState::Running || !hChildStdinWr) {
        return false;
    }

	expectingCompletion = true;
    pushSignal(SignalType::CMD_BUSY);
    std::cout << "cmd busy, pushed signal" << std::endl;

    std::string cmd = command + " & echo " + endMarker + "\n";
	std::cout << "cmd received command: " << cmd << std::endl;
    DWORD bytesWritten;
    BOOL success = WriteFile(hChildStdinWr, cmd.c_str(), static_cast<DWORD>(cmd.length()), &bytesWritten, NULL);

    return success && (bytesWritten == cmd.length());
}

bool ProcessManager::isRunning() const {
    if (hProcess) {
        DWORD exitCode;
        GetExitCodeProcess(hProcess, &exitCode);
        return exitCode == STILL_ACTIVE;
    }
    return false;
}

void ProcessManager::stop() {
    shouldStop = true;

    if (hProcess) {
        TerminateProcess(hProcess, 0);
    }

    if (readThread.joinable()) readThread.join();
    if (errorThread.joinable()) errorThread.join();

    state = ProcessState::Stopped;
}

void ProcessManager::cleanup() {
    // Close all handles
    auto closeHandle = [](HANDLE& handle) {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = NULL;
        }
    };

    closeHandle(hChildStdinRd);
    closeHandle(hChildStdinWr);
    closeHandle(hChildStdoutRd);
    closeHandle(hChildStdoutWr);
    closeHandle(hChildStderrRd);
    closeHandle(hChildStderrWr);
    closeHandle(hProcess);
}
#endif


// ProcessManager_Posix.cpp
#ifndef _WIN32
#include <cstring>
#include <fcntl.h>


bool ProcessManager::start(const std::string& command) {
    if (state == ProcessState::Running) {
        return false;
    }

    // Create pseudo-terminal
    if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) == -1) {
        state = ProcessState::Error;
        return false;
    }

    pid = fork();

    if (pid == -1) {
        close(master_fd);
        close(slave_fd);
        state = ProcessState::Error;
        return false;
    }

    if (pid == 0) { // Child process
        close(master_fd);

        // Make slave pty the controlling terminal
        setsid();
        ioctl(slave_fd, TIOCSCTTY, 0);

        // Duplicate pty slave to stdin, stdout, stderr
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);

        // Close slave fd as it's duplicated
        close(slave_fd);

        // Execute shell
        if (command.empty()) {
            const char* shell = getenv("SHELL");
            if (!shell) shell = "/bin/bash";
            execl(shell, shell, "-i", NULL); // Interactive mode
        }
        else {
            execl("/bin/sh", "/bin/sh", "-c", command.c_str(), NULL);
        }

        // If we get here, exec failed
        exit(EXIT_FAILURE);
    }
    else { // Parent process
        close(slave_fd);

        // Set non-blocking mode for reading
        int flags = fcntl(master_fd, F_GETFL, 0);
        fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

        // Start reader thread
        state = ProcessState::Running;
        shouldStop = false;
        readThread = std::thread(&ProcessManager::readOutput, this);

        return true;
    }
}

void ProcessManager::readOutput() {
    constexpr size_t bufferSize = 4096;
    std::array<char, bufferSize> buffer;

    while (!shouldStop) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(master_fd, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms

        int ready = select(master_fd + 1, &readfds, NULL, NULL, &timeout);

        if (ready > 0 && FD_ISSET(master_fd, &readfds)) {
            ssize_t bytesRead = read(master_fd, buffer.data(), bufferSize - 1);

            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                std::string output(buffer.data());

                checkMarker(output);

                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    outputQueue.push({ output, false });
                }

                if (outputCallback) {
                    outputCallback(output, false);
                }
            }
            else if (bytesRead == 0) { // EOF reached
                break;
            }
        }
        else if (ready < 0) { // Error
            break;
        }

        // Check if process is still alive
        int status;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            break;
        }
    }

    state = ProcessState::Stopped;
}

bool ProcessManager::sendCommand(const std::string& command) {
    if (state != ProcessState::Running || master_fd == -1) {
        return false;
    }

    std::string cmd = command + "\n";
    ssize_t written = write(master_fd, cmd.c_str(), cmd.length());

    return written == static_cast<ssize_t>(cmd.length());
}

bool ProcessManager::isRunning() const {
    if (pid > 0) {
        int status;
        return waitpid(pid, &status, WNOHANG) == 0;
    }
    return false;
}

void ProcessManager::stop() {
    shouldStop = true;

    if (pid > 0) {
        kill(pid, SIGTERM);

        // Wait for process to terminate
        int status;
        waitpid(pid, &status, 0);
    }

    if (readThread.joinable()) readThread.join();

    state = ProcessState::Stopped;
}

void ProcessManager::cleanup() {
    if (master_fd != -1) {
        close(master_fd);
        master_fd = -1;
    }
    pid = -1;
}
#endif

std::vector<std::string> ProcessManager::getOutput() {
    std::lock_guard<std::mutex> lock(outputMutex);
    std::vector<std::string> result;

    while (!outputQueue.empty()) {
        result.push_back(outputQueue.front());
        outputQueue.pop();
    }
    if (result.size()) {
        std::cout << "cmd pop " << result.size() << " outputs from queue" << std::endl;
    }
    return result;
}

void ProcessManager::setOutputCallback(std::function<void(const std::string&, bool)> callback) {
    outputCallback = callback;
}

ProcessState ProcessManager::getState() const {
    return state.load();
}

bool ProcessManager::busy() const {
	return expectingCompletion;
}

void ProcessManager::pushSignal(SignalType signal) {
    std::lock_guard<std::mutex> lock(m_signal_mutex);
    m_signal_queue.push_back(signal);
}

std::vector<SignalType> ProcessManager::popSignals() {
    std::lock_guard<std::mutex> lock(m_signal_mutex);
    std::vector<SignalType> signals(m_signal_queue.begin(), m_signal_queue.end());
    m_signal_queue.clear();
    return signals;
}