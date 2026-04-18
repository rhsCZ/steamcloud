# Installation

This document describes how to build and install SteamCloud Backend from source.

## Prerequisites

Install the following components before building:

- Windows 10 or newer.
- Visual Studio 2022.
- Desktop development with C++ workload.
- MSVC toolset `v145`.
- Windows 10 SDK.
- Git.
- Steam client.

The Steam client must be installed and running when the application is used. The Steam account must have access to the AppID that will be opened in the tool.

## Clone

Clone the repository and enter the project directory:

```powershell
git clone <repository-url> steamcloud-backend
cd steamcloud-backend
```

## Build With Visual Studio

1. Open `steamcloud.sln`.
2. Select `Debug` or `Release`.
3. Select `x86` or `x64`.
4. Build the solution.

The expected build outputs are written to:

```text
output\<Platform>\<Configuration>\
```

For example:

```text
output\x64\Release\
```

The output directory should contain both `steamcloud.exe` and `steam-worker.exe`.

## Build From Command Line

Open a Visual Studio Developer PowerShell or Developer Command Prompt and run:

```powershell
msbuild steamcloud.sln /p:Configuration=Release /p:Platform=x64
```

For a 32-bit build, use:

```powershell
msbuild steamcloud.sln /p:Configuration=Release /p:Platform=x86
```

## Install

SteamCloud Backend does not require a separate installer. Use the files from the selected output directory.

Keep these files in the same directory:

- `steamcloud.exe`
- `steam-worker.exe`

The worker embeds the matching Steam API DLL resource and extracts it at runtime when needed.

## Runtime Setup

Before starting the application:

1. Start Steam and sign in.
2. Make sure Steam Cloud is enabled for the target app.
3. Make sure the signed-in account owns or can access the target AppID.
4. Run `steamcloud.exe`.

## Troubleshooting

- If Steam initialization fails, verify that Steam is running and the entered AppID is valid for the signed-in account.
- If the file list is empty, verify that Steam Cloud is enabled for the app and that the app has cloud files.
- If the worker is busy, wait for the current operation to finish before starting another operation.
- If build fails because the platform toolset is missing, install the matching MSVC toolset in Visual Studio Installer.
