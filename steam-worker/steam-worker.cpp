#include "steamapi_helper.h"
#include <windows.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <thread>

//const char* PIPE_NAME = R"(\\.\pipe\SteamPipe)";

std::vector<std::string> TokenizeCommand(const std::string& input) {
    std::istringstream iss(input);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> std::quoted(token) || iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

int main() {
    char buffer[1024];
    DWORD bytesRead, bytesWritten;
    HANDLE hPipe = CreateFileW(
        L"\\\\.\\pipe\\SteamDlgPipe",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hPipe == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Nepodařilo se připojit na pipe." << std::endl;
        return 1;
    }

	// Send READY message to client - this indicates that the server is ready to process commands
    const char* ready = "READY";
    WriteFile(hPipe, ready, (DWORD)strlen(ready), &bytesWritten, NULL);
    
    bool steamInitialized = false;

    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(hPipe, NULL, 0, NULL, &available, NULL)) {
            std::cerr << "Pipe closed by client.\n";
            break;
        }

        if (available == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
            DWORD err = GetLastError();
            std::cerr << "ReadFile error: " << err << "\n";
            break;
        }

        if (bytesRead == 0) continue;

        buffer[bytesRead] = '\0';
        std::string input(buffer);
        auto tokens = TokenizeCommand(input);
        if (tokens.empty()) continue;

        std::string command = tokens[0];
        std::string response;

        if (command == "connect" && tokens.size() == 2) {
            int appid = std::stoi(tokens[1]);
            if (InitSteamAPI(appid)) {
                response = R"([{"status":"connected"}])";
            }
            else {
                response = R"([{"error":"SteamAPI_Init failed"}])";
            }
        }
        else if (!IsSteamAPIInitialized()) {
            response = R"([{"error":"SteamAPI not initialized"}])";
        }
        else if (command == "list") {
            response = GetFileListJSON();
        }
        else if (command == "quota") {
            response = GetQuotaJSON();
        }
        else if (command == "upload" && tokens.size() >= 2) {
            response = SteamUploadFile(tokens[1]);
        }
        else if (command == "download" && tokens.size() >= 2) {
            response = SteamDownloadFile(tokens[1]);
        }
        else if (command == "delete" && tokens.size() >= 2) {
            response = SteamDeleteFile(tokens[1]);
        }
        else if (command == "disconnect")
        {
			if (IsSteamAPIInitialized()) ShutdownSteamAPI();
            response = R"([{"status":"disconnected"}])";
        }
        else if (command == "exit") {
            response = R"([{"status":"shutting down"}])";
            WriteFile(hPipe, response.c_str(), (DWORD)response.size(), &bytesWritten, NULL);
            break;
        }
        else {
            response = R"([{"error":"unknown command"}])";
        }

        WriteFile(hPipe, response.c_str(), (DWORD)response.size(), &bytesWritten, NULL);
    }

    if (IsSteamAPIInitialized()) ShutdownSteamAPI();
    CloseHandle(hPipe);
    std::cout << "[server] Exiting\n";
    return 0;
}
