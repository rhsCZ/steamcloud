# Usage

SteamCloud Backend manages files stored in Steam Remote Storage for a selected Steam AppID.

## Start

1. Start the Steam client.
2. Sign in with an account that has access to the target app.
3. Run `steamcloud.exe`.
4. Enter the Steam AppID.
5. Click `Connect`.

After a successful connection, the application can list cloud files and display storage quota information.

## Common Operations

### Refresh Files

Click `Refresh` to reload the list of files from Steam Remote Storage.

### Upload A File

Click `Upload`, choose a local file, and wait for the operation to finish. The file is written to Steam Remote Storage.

### Upload A Directory

Click `Dir upload`, choose a directory, and wait for the upload to finish. Files from the selected directory are uploaded to Steam Remote Storage.

### Download Files

Select one or more cloud files and click `Download`. Choose the local destination when prompted.

### Delete Files

Select one or more cloud files and click `Delete`. Confirm the action to remove the selected files from Steam Remote Storage.

### Change Size Unit

Use the size unit controls to display file sizes as bytes, kilobytes, or megabytes.

### Disconnect

Click `Disconnect` to shut down the Steam API session in the worker process.

## Worker Process

`steam-worker.exe` is started and controlled by the GUI. It receives commands through named pipes and returns JSON responses. Do not close the worker manually while an operation is in progress.

## Notes

- Steam must stay running while the tool is connected.
- Only one long-running cloud operation should run at a time.
- Steam Remote Storage behavior depends on the target app configuration in Steamworks.
- Downloaded and uploaded file names must be valid for Steam Remote Storage.
- The application stores UI preferences under `HKEY_CURRENT_USER\Software\steamcloud`.
