# SteamCloud Backend

SteamCloud Backend is a Windows desktop utility for browsing and managing files stored in Steam Remote Storage. The project contains an MFC GUI application (`steamcloud`) and a helper process (`steam-worker`) that communicates with the Steamworks API.

The GUI sends commands to the worker through named pipes. The worker initializes Steam for the selected AppID and performs remote storage operations such as listing files, checking quota, uploading, downloading, and deleting cloud files.

## Features

- Connect to Steam Remote Storage by Steam AppID.
- List cloud files with size, timestamp, existence, and persistence information.
- Upload individual files or whole directories.
- Download selected cloud files.
- Delete selected cloud files.
- Display Steam Cloud quota and transferred data size.
- Build both x86 and x64 Windows binaries.

## Repository Layout

- `steamcloud.sln` - Visual Studio solution for the GUI and worker projects.
- `steamcloud/` - MFC dialog application and UI resources.
- `steam-worker/` - console worker process that wraps Steamworks Remote Storage calls.
- `output/` - default build output directory created by Visual Studio.
- `releases/` - release artifacts, when present.
- `INSTALL.md` - build and installation instructions.
- `USAGE.md` - runtime usage notes.
- `LICENSE.MD` - GNU General Public License version 3.

## Requirements

- Windows 10 or newer.
- Visual Studio 2022 with the Desktop development with C++ workload.
- MSVC toolset `v145`.
- Windows 10 SDK.
- Steam client installed and running.
- A Steam account with access to the target AppID.
- Steam Cloud enabled for the target app.

## Build

Open `steamcloud.sln` in Visual Studio, select the desired platform (`x86` or `x64`) and configuration (`Debug` or `Release`), then build the solution.

The solution builds:

- `steamcloud.exe`
- `steam-worker.exe`

Both projects use `output\$(Platform)\$(Configuration)\` as the output directory.

For detailed setup steps, see [INSTALL.md](INSTALL.md).

## Usage

Run `steamcloud.exe`, enter a Steam AppID, connect, and use the GUI buttons to manage Steam Cloud files for that app.

For detailed usage notes, see [USAGE.md](USAGE.md).

## License

This project is licensed under the GNU General Public License version 3. See [LICENSE.MD](LICENSE.MD).
