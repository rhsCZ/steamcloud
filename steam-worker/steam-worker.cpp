#include "steamapi_helper.h"
#include <windows.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>
#include <queue>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

std::vector<std::string> TokenizeCommand(const std::string& input) {
    std::istringstream iss(input);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> std::quoted(token) || iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

struct CommandTask {
    std::vector<std::string> tokens;
};

bool IsOperationCommand(const std::string& command) {
    return command == "connect"
        || command == "list"
        || command == "quota"
        || command == "upload"
        || command == "download"
        || command == "delete"
        || command == "disconnect";
}

std::string BuildBusyResponse(const std::string& operation) {
    std::ostringstream oss;
    oss << R"([{"status":"busy","operation":")" << EscapeJSONString(operation) << R"("}])";
    return oss.str();
}

std::string BuildStatusResponse(bool operationInProgress, const std::string& operation) {
    std::ostringstream oss;
    const char* steamState = IsSteamAPIInitialized() ? "initialized" : "not_initialized";
    if (operationInProgress) {
        oss << R"([{"status":"working","operation":")" << EscapeJSONString(operation)
            << R"(","steam":")" << steamState << R"("}])";
    }
    else {
        oss << R"([{"status":"idle","steam":")" << steamState << R"("}])";
    }
    return oss.str();
}

void SendResponse(HANDLE hResponsePipe, const std::string& response, std::mutex& responseMutex) {
    std::lock_guard<std::mutex> lock(responseMutex);
    DWORD bytesWritten = 0;
    WriteFile(hResponsePipe, response.c_str(), static_cast<DWORD>(response.size()), &bytesWritten, NULL);
#ifdef _DEBUG
    std::cout << "[server] Response sent: " << response << "\n";
#endif
}

std::string ExecuteCommand(const CommandTask& task) {
    const auto& tokens = task.tokens;
    const std::string& command = tokens[0];

    if (command == "connect" && tokens.size() == 2) {
        int appid = std::stoi(tokens[1]);
        if (InitSteamAPI(appid)) {
            return R"([{"status":"connected"}])";
        }
        return R"([{"status":"SteamAPI_Init failed"}])";
    }

    if (!IsSteamAPIInitialized()) {
        return R"([{"status":"SteamAPI not initialized"}])";
    }

    if (command == "list") {
        return GetFileListJSON();
    }
    if (command == "quota") {
        return GetQuotaJSON();
    }
    if (command == "upload" && tokens.size() >= 2) {
        return SteamUploadFile(tokens[1]);
    }
    if (command == "download" && tokens.size() >= 2) {
        return SteamDownloadFile(tokens[1]);
    }
    if (command == "delete" && tokens.size() >= 2) {
        return SteamDeleteFile(tokens[1]);
    }
    if (command == "disconnect") {
        ShutdownSteamAPI();
        return R"([{"status":"disconnected"}])";
    }

    return R"([{"error":"unknown command"}])";
}

int main() {
    bool readySent = false;
    char buffer[1024];
    DWORD bytesRead = 0;

    HANDLE hRequestPipe = CreateFileW(
        L"\\\\.\\pipe\\SteamDlgRequestPipe",
        GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hRequestPipe == INVALID_HANDLE_VALUE) {
        std::cerr << "Unable to connect pipe." << std::endl;
        return 1;
    }

    HANDLE hResponsePipe = CreateFileW(
        L"\\\\.\\pipe\\SteamDlgResponsePipe",
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hResponsePipe == INVALID_HANDLE_VALUE) {
        CloseHandle(hRequestPipe);
        std::cerr << "Unable to connect response pipe." << std::endl;
        return 1;
    }

    std::mutex responseMutex;
    std::mutex operationMutex;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::queue<CommandTask> commandQueue;
    std::atomic<bool> stopRequested(false);
    std::atomic<bool> operationInProgress(false);
    std::string currentOperation;

    std::thread worker([&]() {
        while (true) {
            CommandTask task;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueCv.wait(lock, [&]() { return stopRequested.load() || !commandQueue.empty(); });
                if (stopRequested.load() && commandQueue.empty()) {
                    break;
                }
                task = std::move(commandQueue.front());
                commandQueue.pop();
            }

            std::string response = ExecuteCommand(task);
            SendResponse(hResponsePipe, response, responseMutex);

            {
                std::lock_guard<std::mutex> lock(operationMutex);
                currentOperation.clear();
            }
            operationInProgress.store(false);
        }
    });

    while (true) {
        if (!readySent) {
            readySent = true;
            SendResponse(hResponsePipe, "READY", responseMutex);
#ifdef _DEBUG
            std::cout << "[server] Ready to process commands.\n";
#endif
        }

        DWORD available = 0;
        if (!PeekNamedPipe(hRequestPipe, NULL, 0, NULL, &available, NULL)) {
            std::cerr << "Pipe closed by client.\n";
            break;
        }

        if (available == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!ReadFile(hRequestPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
            DWORD err = GetLastError();
            std::cerr << "ReadFile error: " << err << "\n";
            break;
        }

        if (bytesRead == 0) {
            continue;
        }

        buffer[bytesRead] = '\0';
        std::string input(buffer);
        auto tokens = TokenizeCommand(input);
        if (tokens.empty()) {
            continue;
        }

        const std::string command = tokens[0];
#ifdef _DEBUG
        if (command != ".") {
            std::cout << "[server] Received command: " << input << "\n";
        }
#endif

        if (command == ".") {
            continue;
        }

        if (command == "status") {
            bool busy = operationInProgress.load();
            std::string runningOperation;
            {
                std::lock_guard<std::mutex> lock(operationMutex);
                runningOperation = currentOperation;
            }
            SendResponse(hResponsePipe, BuildStatusResponse(busy, runningOperation), responseMutex);
            continue;
        }

        if (command == "exit") {
            SendResponse(hResponsePipe, R"([{"status":"shutting down"}])", responseMutex);
            stopRequested.store(true);
            queueCv.notify_one();
            break;
        }

        if (!IsOperationCommand(command)) {
            SendResponse(hResponsePipe, R"([{"error":"unknown command"}])", responseMutex);
            continue;
        }

        bool expected = false;
        if (!operationInProgress.compare_exchange_strong(expected, true)) {
            std::string runningOperation;
            {
                std::lock_guard<std::mutex> lock(operationMutex);
                runningOperation = currentOperation;
            }
            if (runningOperation.empty()) {
                runningOperation = "unknown";
            }
            SendResponse(hResponsePipe, BuildBusyResponse(runningOperation), responseMutex);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(operationMutex);
            currentOperation = command;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            commandQueue.push(CommandTask{ std::move(tokens) });
        }
        queueCv.notify_one();
    }

    stopRequested.store(true);
    queueCv.notify_one();
    if (worker.joinable()) {
        worker.join();
    }

    if (IsSteamAPIInitialized()) {
        ShutdownSteamAPI();
    }

    CloseHandle(hResponsePipe);
    CloseHandle(hRequestPipe);
    std::cout << "[server] Exiting\n";
    return 0;
}
