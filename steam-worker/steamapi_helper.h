#pragma once
#include <string>
#include "steam/steam_api.h"
#include <Windows.h>
#include <iostream>

bool InitSteamAPI(int appid);
void ShutdownSteamAPI();

std::string GetFileListJSON();
std::string GetQuotaJSON();
std::string SteamUploadFile(const std::string& uploadSpec);
std::string SteamDownloadFile(const std::string& input);
std::string SteamDeleteFile(const std::string& semicolonSeparatedNames);
std::string EscapeJSONString(const std::string& input);
bool IsSteamAPIInitialized();