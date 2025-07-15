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
	bool once = false;
	bool ping = false;
    char buffer[1024];
    DWORD bytesRead, bytesWritten;
    HANDLE hRequestPipe = CreateFileW(
        L"\\\\.\\pipe\\SteamDlgRequestPipe",
        GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hRequestPipe == INVALID_HANDLE_VALUE)
    {
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

    if (hResponsePipe == INVALID_HANDLE_VALUE)
    {
        if (hRequestPipe == INVALID_HANDLE_VALUE)
        {
			CloseHandle(hRequestPipe);
        }
        std::cerr << "Unable to connect response pipe." << std::endl;
		return 1;
    }
	// Send READY message to client - this indicates that the server is ready to process commands
    const char* ready = "READY";
    bool steamInitialized = false;

    while (true) {
        if(!once) {
			once = true;
            WriteFile(hResponsePipe, ready, (DWORD)strlen(ready), &bytesWritten, NULL);
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

        if (bytesRead == 0) continue;

        buffer[bytesRead] = '\0';
        std::string input(buffer);
        auto tokens = TokenizeCommand(input);
        if (tokens.empty()) continue;

        std::string command = tokens[0];
        std::string response;
#ifdef _DEBUG
        if (command != ".")
        {
            // Print the command received for debugging purposes, except for the ping command it would be messy
            std::cout << "[server] Received command: " << input << "\n";
        }
#endif
        if (command == "connect" && tokens.size() == 2) {
            int appid = std::stoi(tokens[1]);
            if (InitSteamAPI(appid)) {
                response = R"([{"status":"connected"}])";
            }
            else {
                response = R"([{"status":"SteamAPI_Init failed"}])";
            }
        }
        else if (!IsSteamAPIInitialized() && command != ".") {
            response = R"([{"status":"SteamAPI not initialized"}])";
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
            WriteFile(hResponsePipe, response.c_str(), (DWORD)response.size(), &bytesWritten, NULL);
            break;
        }
        else if (command == ".")
        {
			ping = true;
        }
        else {
            response = R"([{"error":"unknown command"}])";
        }
        if(!ping)
        {
        WriteFile(hResponsePipe, response.c_str(), (DWORD)response.size(), &bytesWritten, NULL);
#ifdef _DEBUG
		std::cout << "[server] Response sent: " << response << "\n";
#endif
        }
        else
        {
			ping = false;
        }
    }

    if (IsSteamAPIInitialized()) ShutdownSteamAPI();
    CloseHandle(hResponsePipe);
	CloseHandle(hRequestPipe);
    std::cout << "[server] Exiting\n";
    return 0;
}
