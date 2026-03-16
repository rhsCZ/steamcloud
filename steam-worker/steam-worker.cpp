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
#include <chrono>

#ifndef IDR_STEAMDLL
#define IDR_STEAMDLL 101
#endif

bool ExtractResourceToFile(HINSTANCE hInstance, int resourceId, const std::wstring& outPath) {
    HRSRC hRes = FindResourceW(hInstance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) return false;

    DWORD size = SizeofResource(hInstance, hRes);
    HGLOBAL hData = LoadResource(hInstance, hRes);
    if (!hData) return false;

    void* pData = LockResource(hData);
    if (!pData) return false;

    HANDLE hFile = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, pData, size, &written, NULL);
    CloseHandle(hFile);
    return ok && written == size;
}

bool EnsureSteamApiDllAvailable(std::wstring& errorMessage) {
#if defined(_WIN64)
    const wchar_t* dllName = L"steam_api64.dll";
#else
    const wchar_t* dllName = L"steam_api.dll";
#endif

    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(NULL, modulePath, MAX_PATH) == 0) {
        errorMessage = L"Unable to read module path.";
        return false;
    }

    std::wstring exePath(modulePath);
    size_t slashPos = exePath.find_last_of(L"\\/");
    if (slashPos == std::wstring::npos) {
        errorMessage = L"Invalid module path.";
        return false;
    }

    std::wstring exeDir = exePath.substr(0, slashPos);
    std::wstring dllPathInExeDir = exeDir + L"\\" + dllName;

    DWORD attrs = GetFileAttributesW(dllPathInExeDir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        SetDllDirectoryW(exeDir.c_str());
        return true;
    }

    if (ExtractResourceToFile(GetModuleHandleW(NULL), IDR_STEAMDLL, dllPathInExeDir)) {
        SetDllDirectoryW(exeDir.c_str());
        return true;
    }

    wchar_t tempPath[MAX_PATH] = {};
    DWORD tempLen = GetTempPathW(MAX_PATH, tempPath);
    if (tempLen == 0 || tempLen > MAX_PATH) {
        errorMessage = L"Unable to get TEMP path.";
        return false;
    }

    std::wstring tempDir(tempPath);
    if (!tempDir.empty() && (tempDir.back() == L'\\' || tempDir.back() == L'/')) {
        tempDir.pop_back();
    }

    std::wstring dllPathInTemp = tempDir + L"\\" + dllName;
    if (!ExtractResourceToFile(GetModuleHandleW(NULL), IDR_STEAMDLL, dllPathInTemp)) {
        errorMessage = L"Unable to extract Steam API DLL to executable folder or TEMP.";
        return false;
    }

    SetDllDirectoryW(tempDir.c_str());
    return true;
}

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
    bool respondToPipe = true;
    bool respondToStdout = false;
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

bool SendResponse(HANDLE& hResponsePipe, const std::string& response, std::mutex& responseMutex, std::mutex& pipeHandleMutex) {
    std::lock_guard<std::mutex> lock(responseMutex);
    std::lock_guard<std::mutex> pipeLock(pipeHandleMutex);
    if (hResponsePipe == INVALID_HANDLE_VALUE || hResponsePipe == NULL) {
        return false;
    }
    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(hResponsePipe, response.c_str(), static_cast<DWORD>(response.size()), &bytesWritten, NULL);
#ifdef _DEBUG
    if (ok) {
        std::cout << "[server] Response sent: " << response << "\n";
    }
#endif
    return ok == TRUE;
}

void SendConsoleResponse(const std::string& response) {
    std::cout << response << "\n";
}

void EmitResponse(
    const CommandTask& task,
    const std::string& response,
    HANDLE& hResponsePipe,
    std::mutex& responseMutex,
    std::mutex& pipeHandleMutex)
{
    if (task.respondToPipe) {
        SendResponse(hResponsePipe, response, responseMutex, pipeHandleMutex);
    }
    if (task.respondToStdout) {
        SendConsoleResponse(response);
    }
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
    std::wstring dllError;
    if (!EnsureSteamApiDllAvailable(dllError)) {
        std::wcerr << L"[server] " << dllError << L"\n";
        return 1;
    }

    bool readySent = false;
    char buffer[1024];
    DWORD bytesRead = 0;
    HANDLE hRequestPipe = INVALID_HANDLE_VALUE;
    HANDLE hResponsePipe = INVALID_HANDLE_VALUE;

    std::mutex responseMutex;
    std::mutex pipeHandleMutex;
    std::mutex operationMutex;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::queue<CommandTask> commandQueue;
    std::atomic<bool> stopRequested(false);
    std::atomic<bool> operationInProgress(false);
    std::atomic<ULONGLONG> lastRequestTick(GetTickCount64());
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
            EmitResponse(task, response, hResponsePipe, responseMutex, pipeHandleMutex);

            {
                std::lock_guard<std::mutex> lock(operationMutex);
                currentOperation.clear();
            }
            operationInProgress.store(false);
        }
    });

    auto ClosePipeHandles = [&]() {
        std::lock_guard<std::mutex> lock(pipeHandleMutex);
        if (hResponsePipe != INVALID_HANDLE_VALUE && hResponsePipe != NULL) {
            DisconnectNamedPipe(hResponsePipe);
            CloseHandle(hResponsePipe);
            hResponsePipe = INVALID_HANDLE_VALUE;
        }
        if (hRequestPipe != INVALID_HANDLE_VALUE && hRequestPipe != NULL) {
            DisconnectNamedPipe(hRequestPipe);
            CloseHandle(hRequestPipe);
            hRequestPipe = INVALID_HANDLE_VALUE;
        }
    };

    auto ConnectNamedPipeWithTimeout = [&stopRequested](HANDLE pipeHandle, DWORD timeoutMs) -> bool {
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (ov.hEvent == NULL) {
            return false;
        }

        BOOL connected = ConnectNamedPipe(pipeHandle, &ov);
        if (connected) {
            CloseHandle(ov.hEvent);
            return true;
        }

        DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED) {
            CloseHandle(ov.hEvent);
            return true;
        }

        if (err != ERROR_IO_PENDING) {
            CloseHandle(ov.hEvent);
            return false;
        }

        DWORD wait = WaitForSingleObject(ov.hEvent, timeoutMs);
        if (wait == WAIT_TIMEOUT) {
            CancelIo(pipeHandle);
            CloseHandle(ov.hEvent);
            return false;
        }
        if (stopRequested.load()) {
            CancelIo(pipeHandle);
            CloseHandle(ov.hEvent);
            return false;
        }

        DWORD transferred = 0;
        BOOL result = GetOverlappedResult(pipeHandle, &ov, &transferred, FALSE);
        if (!result && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(ov.hEvent);
            return false;
        }

        CloseHandle(ov.hEvent);
        return true;
    };

    auto EnsurePipeConnected = [&]() -> bool {
        {
            std::lock_guard<std::mutex> lock(pipeHandleMutex);
            if (hRequestPipe != INVALID_HANDLE_VALUE && hResponsePipe != INVALID_HANDLE_VALUE) {
                return true;
            }
        }

        HANDLE newRequestPipe = CreateNamedPipeW(
            L"\\\\.\\pipe\\SteamDlgRequestPipe",
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            4096,
            4096,
            0,
            NULL);

        if (newRequestPipe == INVALID_HANDLE_VALUE) {
            return false;
        }

        HANDLE newResponsePipe = CreateNamedPipeW(
            L"\\\\.\\pipe\\SteamDlgResponsePipe",
            PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            4096,
            4096,
            0,
            NULL);

        if (newResponsePipe == INVALID_HANDLE_VALUE) {
            CloseHandle(newRequestPipe);
            return false;
        }

        bool requestConnected = ConnectNamedPipeWithTimeout(newRequestPipe, 2000);
        bool responseConnected = ConnectNamedPipeWithTimeout(newResponsePipe, 2000);
        if (!requestConnected || !responseConnected) {
            DisconnectNamedPipe(newRequestPipe);
            DisconnectNamedPipe(newResponsePipe);
            CloseHandle(newRequestPipe);
            CloseHandle(newResponsePipe);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(pipeHandleMutex);
            if (hRequestPipe != INVALID_HANDLE_VALUE && hRequestPipe != NULL) {
                DisconnectNamedPipe(hRequestPipe);
                CloseHandle(hRequestPipe);
            }
            if (hResponsePipe != INVALID_HANDLE_VALUE && hResponsePipe != NULL) {
                DisconnectNamedPipe(hResponsePipe);
                CloseHandle(hResponsePipe);
            }
            hRequestPipe = newRequestPipe;
            hResponsePipe = newResponsePipe;
        }

        return true;
    };

    auto IsPipeDisconnectedError = [](DWORD err) -> bool {
        return err == ERROR_BROKEN_PIPE
            || err == ERROR_PIPE_NOT_CONNECTED
            || err == ERROR_NO_DATA
            || err == ERROR_INVALID_HANDLE
            || err == ERROR_BAD_PIPE;
    };

#ifdef _DEBUG
    std::thread stdinThread([&]() {
        std::string input;
        while (!stopRequested.load()) {
            if (!std::getline(std::cin, input)) {
                break;
            }

            lastRequestTick.store(GetTickCount64());
            auto tokens = TokenizeCommand(input);
            if (tokens.empty()) {
                continue;
            }

            const std::string& command = tokens[0];
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
                SendConsoleResponse(BuildStatusResponse(busy, runningOperation));
                continue;
            }

            if (command == "exit") {
                SendConsoleResponse(R"([{"status":"shutting down"}])");
                stopRequested.store(true);
                queueCv.notify_one();
                break;
            }

            if (!IsOperationCommand(command)) {
                SendConsoleResponse(R"([{"error":"unknown command"}])");
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
                SendConsoleResponse(BuildBusyResponse(runningOperation));
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(operationMutex);
                currentOperation = command;
            }

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                commandQueue.push(CommandTask{ std::move(tokens), false, true });
            }
            queueCv.notify_one();
        }
    });
    stdinThread.detach();
#endif

    while (true) {
        if (stopRequested.load()) {
            break;
        }

        if (GetTickCount64() - lastRequestTick.load() >= 5ULL * 60ULL * 1000ULL) {
            std::cout << "[server] Idle timeout reached (5 minutes), shutting down.\n";
            break;
        }

        if (!EnsurePipeConnected()) {
            if (stopRequested.load()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        if (!readySent) {
            readySent = true;
            SendResponse(hResponsePipe, "READY", responseMutex, pipeHandleMutex);
#ifdef _DEBUG
            std::cout << "[server] Ready to process commands.\n";
#endif
        }

        HANDLE requestPipeHandle = INVALID_HANDLE_VALUE;
        {
            std::lock_guard<std::mutex> lock(pipeHandleMutex);
            requestPipeHandle = hRequestPipe;
        }

        DWORD available = 0;
        if (!PeekNamedPipe(requestPipeHandle, NULL, 0, NULL, &available, NULL)) {
            DWORD err = GetLastError();
            if (IsPipeDisconnectedError(err)) {
                ClosePipeHandles();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            std::cerr << "PeekNamedPipe error: " << err << "\n";
            break;
        }

        if (available == 0) {
            if (stopRequested.load()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!ReadFile(requestPipeHandle, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
            DWORD err = GetLastError();
            if (IsPipeDisconnectedError(err)) {
                ClosePipeHandles();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            std::cerr << "ReadFile error: " << err << "\n";
            break;
        }

        if (bytesRead == 0) {
            continue;
        }
        lastRequestTick.store(GetTickCount64());

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
            EmitResponse(CommandTask{ {}, true, false }, BuildStatusResponse(busy, runningOperation), hResponsePipe, responseMutex, pipeHandleMutex);
            continue;
        }

        if (command == "exit") {
            EmitResponse(CommandTask{ {}, true, false }, R"([{"status":"shutting down"}])", hResponsePipe, responseMutex, pipeHandleMutex);
            stopRequested.store(true);
            queueCv.notify_one();
            break;
        }

        if (!IsOperationCommand(command)) {
            EmitResponse(CommandTask{ {}, true, false }, R"([{"error":"unknown command"}])", hResponsePipe, responseMutex, pipeHandleMutex);
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
            EmitResponse(CommandTask{ {}, true, false }, BuildBusyResponse(runningOperation), hResponsePipe, responseMutex, pipeHandleMutex);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(operationMutex);
            currentOperation = command;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            commandQueue.push(CommandTask{ std::move(tokens), true, false });
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

    ClosePipeHandles();
    std::cout << "[server] Exiting\n";
    return 0;
}
