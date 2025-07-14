#include "steamapi_helper.h"
#include <sstream>
#include <vector>
#include <fstream>
#include <filesystem>

static bool g_steamInitialized = false;


bool InitSteamAPI(int appid) {
    if (g_steamInitialized) {
        SteamAPI_Shutdown();
        g_steamInitialized = false;
    }

    std::string appid_str = std::to_string(appid);
    SetEnvironmentVariableA("SteamAppID", appid_str.c_str());

    if (!SteamAPI_Init()) {
        return false;
    }

    g_steamInitialized = true;
    SteamAPI_RunCallbacks();
    return true;
}

bool IsSteamAPIInitialized() {
    return g_steamInitialized;
}

void ShutdownSteamAPI() {
    if (g_steamInitialized) {
        SteamAPI_Shutdown();
        g_steamInitialized = false;
    }
}

std::string EscapeJSONString(const std::string& input) {
    std::ostringstream oss;
    for (char c : input) {
        switch (c) {
        case '\"': oss << "\\\""; break;
        case '\\': oss << "\\\\"; break;
        case '\b': oss << "\\b"; break;
        case '\f': oss << "\\f"; break;
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
                oss << "\\u" << std::hex << std::uppercase << (int)c;
            else
                oss << c;
        }
    }
    return oss.str();
}

std::string GetFileListJSON() {
    int32 count = SteamRemoteStorage()->GetFileCount();
    std::ostringstream oss;
    oss << "[";

    for (int i = 0; i < count; ++i) {
        int32 size = 0;
        const char* file = SteamRemoteStorage()->GetFileNameAndSize(i, &size);
        int64 ts = SteamRemoteStorage()->GetFileTimestamp(file);
        bool exists = SteamRemoteStorage()->FileExists(file);
        bool persist = SteamRemoteStorage()->FilePersisted(file);
        oss << "{"
            << R"("name":")" << EscapeJSONString(file) << "\","
            << R"("size":)" << size << ","
            << R"("timestamp":)" << ts << ","
            << R"("exists":)" << (exists ? "true" : "false") << ","
			<< R"("persistent":)" << (persist ? "true" : "false")
            << "}";
        if (i < count - 1) {
            oss << ",";
        }
    }

    oss << "]";
    return oss.str();
}

std::string GetQuotaJSON() {
    uint64 total = 0, available = 0;
    SteamRemoteStorage()->GetQuota(&total, &available);
    uint64 used = total - available;

    std::ostringstream oss;
    oss << "[{"
        << R"("used":)" << used << ","
        << R"("available":)" << available << ","
        << R"("total":)" << total
        << "}]";

    return oss.str();
}

std::string SteamUploadFile(const std::string& uploadSpec) {
    std::istringstream entryStream(uploadSpec);
    std::string pair;
    std::ostringstream oss;
    oss << "[";

    constexpr ULONG MAX_FILE_SIZE = 102400000;
    bool first = true;

    while (std::getline(entryStream, pair, ';')) {
        if (pair.empty()) continue;

        size_t commaPos = pair.find(',');
        if (commaPos == std::string::npos || commaPos == 0 || commaPos == pair.length() - 1) {
            if (!first) oss << ",";
            oss << R"({"error":"invalid format","input":")" << EscapeJSONString(pair) << "\"}";
            first = false;
            continue;
        }

        std::string cloudName = pair.substr(0, commaPos);
        std::string localPath = pair.substr(commaPos + 1);

        std::ifstream file(localPath, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            if (!first) oss << ",";
            oss << R"({"name":")" << EscapeJSONString(cloudName)
                << R"(","error":"Failed to open file"})";
            first = false;
            continue;
        }

        file.seekg(0, file.end);
        ULONG length = static_cast<ULONG>(file.tellg());
        file.seekg(0, file.beg);

        if (length >= MAX_FILE_SIZE) {
            file.close();
            if (!first) oss << ",";
            oss << R"({"name":")" << EscapeJSONString(cloudName)
                << R"(","error":"File too large"})";
            first = false;
            continue;
        }

        std::vector<std::byte> buffer(length);
        file.read(reinterpret_cast<char*>(buffer.data()), length);
        file.close();

        bool success = SteamRemoteStorage()->FileWrite(
            cloudName.c_str(),
            reinterpret_cast<char*>(buffer.data()),
            length
        );

        if (!first) oss << ",";
        if (success) {
            oss << "{"
                << R"("name":")" << EscapeJSONString(cloudName) << "\","
                << R"("status":"uploaded",)"
                << R"("size":)" << length
                << "}";
        }
        else {
            oss << "{"
                << R"("name":")" << EscapeJSONString(cloudName) << "\","
                << R"("error":"Steam FileWrite failed")"
                << "}";
        }

        first = false;
    }

    oss << "]";
    std::cout << oss.str() << std::endl;
    return oss.str();
}

std::string SteamDownloadFile(const std::string& input) {
    std::istringstream iss(input);
    std::string token;
    std::vector<std::string> tokens;

	//Split the input by semicolon
    while (std::getline(iss, token, ';')) {
        if (!token.empty()) tokens.push_back(token);
    }

    if (tokens.size() < 2) {
        return R"([{"error":"missing file(s) or destination"}])";
    }

    std::ostringstream oss;
    oss << "[";
    bool first = true;

    std::string destinationPath = tokens.back();
    tokens.pop_back();
    std::vector<std::string> cloudFiles = tokens;

    bool isDirectory = false;
    DWORD attribs = GetFileAttributesA(destinationPath.c_str());
    if (attribs != INVALID_FILE_ATTRIBUTES && (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
        isDirectory = true;
    }

	//If multiple files are specified, the destination must be a directory
    if (cloudFiles.size() > 1 && !isDirectory) {
        return R"([{"error":"destination must be directory for multiple files"}])";
    }

    for (const auto& cloudName : cloudFiles) {
        int32 fileSize = SteamRemoteStorage()->GetFileSize(cloudName.c_str());
        if (fileSize <= 0) {
            if (!first) oss << ",";
            oss << R"({"name":")" << EscapeJSONString(cloudName)
                << R"(","error":"file not found in cloud"})";
            first = false;
            continue;
        }

        std::vector<std::byte> buffer(fileSize);
        int32 bytesRead = SteamRemoteStorage()->FileRead(
            cloudName.c_str(),
            reinterpret_cast<char*>(buffer.data()),
            fileSize
        );

        if (bytesRead <= 0) {
            if (!first) oss << ",";
            oss << R"({"name":")" << EscapeJSONString(cloudName)
                << R"(","error":"Steam FileRead failed"})";
            first = false;
            continue;
        }

        std::filesystem::path outPath;
        if (isDirectory) {
            std::filesystem::path fileNameOnly = std::filesystem::u8path(cloudName).filename();
            outPath = std::filesystem::u8path(destinationPath) / fileNameOnly;
        }
        else {
            outPath = std::filesystem::u8path(destinationPath);
        }

		// Check if the file already exists and delete it if necessary
        HANDLE existing = CreateFileW(outPath.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (existing != INVALID_HANDLE_VALUE) {
            CloseHandle(existing);
            if (!DeleteFileW(outPath.wstring().c_str())) {
                if (!first) oss << ",";
                oss << R"({"name":")" << EscapeJSONString(cloudName)
                    << R"(","error":"failed to delete existing file"})";
                first = false;
                continue;
            }
        }

		//Create the file
        HANDLE created = CreateFileW(outPath.wstring().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (created == INVALID_HANDLE_VALUE) {
            if (!first) oss << ",";
            oss << R"({"name":")" << EscapeJSONString(cloudName)
                << R"(","error":"failed to create file on disk"})";
            first = false;
            continue;
        }
        CloseHandle(created);

        std::ofstream file(outPath, std::ios::binary);
        if (!file.is_open()) {
            if (!first) oss << ",";
            oss << R"({"name":")" << EscapeJSONString(cloudName)
                << R"(","error":"failed to open file for writing"})";
            first = false;
            continue;
        }

        file.write(reinterpret_cast<char*>(buffer.data()), bytesRead);
        file.close();

        if (!first) oss << ",";
        oss << "{"
            << R"("name":")" << EscapeJSONString(cloudName) << "\","
            << R"("status":"downloaded",)"
            << R"("size":)" << bytesRead
            << "}";
        first = false;
    }

    oss << "]";
    std::cout << oss.str() << std::endl;
    return oss.str();
}

std::string SteamDeleteFile(const std::string& semicolonSeparatedNames) {
    std::istringstream iss(semicolonSeparatedNames);
    std::string token;
    std::ostringstream oss;

    std::vector<std::string> validNames;

	//Split the input by semicolon
    while (std::getline(iss, token, ';')) {
        if (!token.empty()) {
            validNames.push_back(token);
        }
    }

	// No valid names provided
    if (validNames.empty()) {
        return R"([{"error":"no files given"}])";
    }

	// Delete each file and collect results
    oss << "[";
    for (size_t i = 0; i < validNames.size(); ++i) {
        const std::string& name = validNames[i];
        bool success = SteamRemoteStorage()->FileDelete(name.c_str());

        oss << "{"
            << R"("name":")" << EscapeJSONString(name) << "\","
            << R"("deleted":)" << (success ? "true" : "false")
            << "}";

        if (i < validNames.size() - 1) {
            oss << ",";
        }
    }
    oss << "]";
    std::cout << oss.str() << std::endl;
    return oss.str();
}

