
// hasher2Dlg.h : header file
//
#pragma once
#include "afxdialogex.h"
#include "afxwin.h"
#include <vector>
#include <iostream>
#include "functions.h"
#include <sstream>
#include <iterator>
#include <thread>
#include <tlhelp32.h>
#include <algorithm>
#include <map>
#include <set>
#include "json.hpp"
using json = nlohmann::json;
using namespace std;
#define MAX_UNICODE_PATH 32766
#define bufferSize 10
#define WM_SHOWPAGE WM_APP+2
#define WM_TRAY_ICON_NOTIFY_MESSAGE (WM_USER + 1)
#define MAX_FILES_COUNT (ULONG)10
#define MAX_FILES_BUFFER_SIZE (MAX_FILES_COUNT*MAX_UNICODE_PATH)-1
int RegCrtKey(HKEY key, LPSTR keyloc, REGSAM access);
template<typename T>
int RegGetKey(HKEY key, LPCSTR keyloc, DWORD type, REGSAM access, LPCSTR name, T& outdata);
template<typename T>
bool RegSetKey(HKEY key, LPCSTR keyloc, DWORD type, REGSAM access, LPCSTR name, const T& indata);
template<typename T>
bool RegSetKey(HKEY key, LPCSTR keyloc, DWORD type, REGSAM access, LPCSTR name, const T* indata);
template<size_t N>
int RegGetKey(HKEY key, LPCSTR keyloc, DWORD type, REGSAM access, LPCSTR name, char(&outdata)[N]);
size_t get_data_size(const std::wstring& s);
size_t get_data_size(const std::string& s);
template<size_t N>
size_t get_data_size(const wchar_t(&data)[N]);
template<size_t N>
size_t get_data_size(const char(&data)[N]);
template<typename T>
size_t get_data_size(const T& data);
bool ExtractResourceToFile(HINSTANCE hInstance, LPCWSTR resourceName, LPCWSTR resourceType, const CString& outPath);
//int CALLBACK CompareFunc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);)
struct SortInfo {
	int column;
	bool ascending;
};
struct FileRow {
	CString name;
	__time64_t timestamp;
	int size;
	bool persisted;
	bool exists;
};
class CsteamcloudDlg : public CDialog
{
private:
	BOOL m_bMinimizeToTray;
	BOOL			m_bTrayIconVisible;
	CMenu			m_mnuTrayMenu;
	UINT			m_nDefaultMenuItem;
	std::map<CString, int64_t> m_fileSizesBytes;
	uint64_t m_quotaUsed = 0;
	uint64_t m_quotaTotal = 0;
	uint64_t m_quotaAvailable = 0;
	bool pipeconnected = false;
	bool pipeblocked = false;
	bool m_brokenPipe = false;
	bool m_statusrequestpipe = false;
	bool m_statusresponsepipe = false;
	bool m_RequestThreadEnabled = false;
	bool m_ResponseThreadEnabled = false;
	bool m_RequestThreadRunning = false;
	bool m_ResponseThreadRunning = false;
	bool m_RequestThreadWaiting = false;
	bool m_ResponseThreadWaiting = false;
	bool active = false;
	int m_nSortedColumn = -1;
	bool m_bSortAscending = true;
	bool firstRun = true;
	void RequestPipeThread();
	void ResponsePipeThread();
	thread m_RequestpipeThread;
	thread m_ResponsepipeThread;
	std::vector<FileRow> m_fileRowData;
	afx_msg void OnLvnColumnClickListFiles(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg LRESULT OnTrayNotify(WPARAM wParam, LPARAM lParam);
	//PFNLVCOMPARE CompareFunc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
	static int CALLBACK CompareFunc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);
	DWORD GetProcessPIDByName(const wchar_t* name);
	void KillAllSteamWorkerProcesses();
// Construction
public:
	NOTIFYICONDATA	m_nidIconData;
	CsteamcloudDlg(CWnd* pParent = nullptr);	// standard constructor
	~CsteamcloudDlg(); //
	void TraySetMinimizeToTray(BOOL bMinimizeToTray = TRUE);
	BOOL TraySetMenu(UINT nResourceID, UINT nDefaultPos = 0);
	BOOL TraySetMenu(HMENU hMenu, UINT nDefaultPos = 0);
	BOOL TraySetMenu(LPCTSTR lpszMenuName, UINT nDefaultPos = 0);
	BOOL TrayUpdate();
	BOOL TrayShow();
	BOOL TrayHide();
	void TraySetToolTip(LPCTSTR lpszToolTip);
	void TraySetIcon(HICON hIcon);
	void TraySetIcon(UINT nResourceID);
	void TraySetIcon(LPCTSTR lpszResourceName);
	BOOL TrayIsVisible();
	virtual void OnTrayLButtonDown(CPoint pt);
	virtual void OnTrayLButtonDblClk(CPoint pt);
	virtual void OnTrayRButtonDown(CPoint pt);
	virtual void OnTrayRButtonDblClk(CPoint pt);
	virtual void OnTrayMouseMove(CPoint pt);

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_STEAMCLOUD_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	std::vector<CString> m_appidList;
	std::vector<CString> m_appidList_orig;
	std::map<char, CString> m_appidMap;
	string m_appList_keyOrder;
	HANDLE m_hRequestPipe = NULL;
	HANDLE m_hResponsePipe = NULL;
	HANDLE m_hWorkerProcess = NULL;
	void LoadComboBoxHistory();
	void SaveComboBoxHistory();
	void UpdateAppIdHistoryFromInput();

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDestroy();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
	DECLARE_MESSAGE_MAP()
	
public:
	static inline wchar_t *returned;
	afx_msg void OnBnClickedExit();
	bool trayenable = false;
	bool minimizeen = false;
	bool init = false;
	int sizeunit = 0;
	//SteamAPICallCompleted_t steamcall;
	CButton* trayen = nullptr;
	CButton* Bytes = nullptr;
	CButton* Kbytes = nullptr;
	CButton* Mbytes = nullptr;
	CButton* checkbox = nullptr;
	CButton* upload = nullptr;
	CButton* download = nullptr;
	CButton* connect = nullptr;
	CButton* exitb = nullptr;
	CButton* cancel = nullptr;
	CButton* uploaddir = nullptr;
	CButton* deletefile = nullptr;
	CButton* refresh = nullptr;
	CButton* disconnect = nullptr;
	CStatic* quota = nullptr;
	CComboBox* inputappid = nullptr;
	HKEY traykey = nullptr;
	CListCtrl* listfiles = nullptr;
	DWORD traykeyvalue = 0;
	unsigned long type = 0;
	DWORD keycreate = 0;
	afx_msg void OnOpen();
	afx_msg void OnMinimize();
	afx_msg void OnBnClickedMinEn();
	afx_msg void OnBnClickedTrayEn();
	PCHAR* CommandLineToArgvA(PCHAR CmdLine, int* _argc);
	afx_msg void OnBnClickedConnect();
	afx_msg void OnBnClickedDelete();
	afx_msg void OnBnClickedUpload();
	afx_msg void OnBnClickedDirupload();
	afx_msg void OnBnClickedDownload();
	void GetFiles();
	void Clearlist();
	void UpdateFileSizesDisplay();
	bool ReadFromPipeWithTimeout(DWORD timeoutMs, std::string& output);
	afx_msg void OnBnClickedRefresh();
	afx_msg void OnBnClickedDisconnect();
	afx_msg void OnBnClickedBytes();
	afx_msg void OnBnClickedKbytes();
	afx_msg void OnBnClickedMbytes();
};

static int RegCrtKey(HKEY key, LPSTR keyloc, REGSAM access)
{
	HKEY keyval;
	int err;
	char errorbuf[200];
	DWORD dispvalue;
	err = RegCreateKeyExA(key, keyloc, NULL, NULL, REG_OPTION_NON_VOLATILE, access, NULL, &keyval, &dispvalue);
	CloseHandle(keyval);
	if (err == ERROR_SUCCESS)
	{
		if (dispvalue == REG_CREATED_NEW_KEY)
		{
			return 1;
		}
		else
		{
			return 2;
		}
	}
	else
	{
		sprintf_s(errorbuf, "%i\n", err);
		ASSERT(errorbuf);
		return 0;
	}
	//return onerr;
}

template<typename T>
static size_t get_data_size(const T& data)
{
	return sizeof(T);
}

template<size_t N>
static size_t get_data_size(const char(&data)[N])
{
	return strlen(data) + 1;
}

template<size_t N>
static size_t get_data_size(const wchar_t(&data)[N])
{
	return (wcslen(data) + 1) * sizeof(wchar_t);
}

static size_t get_data_size(const std::string& s)
{
	return s.size() + 1;
}

static size_t get_data_size(const std::wstring& s)
{
	return (s.size() + 1) * sizeof(wchar_t);
}

template<typename T>
static bool RegSetKey(HKEY key, LPCSTR keyloc, DWORD type, REGSAM access, LPCSTR name, const T& indata)
{
	HKEY keyval = nullptr;
	LONG err = RegOpenKeyExA(key, keyloc, 0, access, &keyval);
	if (err != ERROR_SUCCESS)
		return false;

	size_t size = get_data_size(indata);
	const BYTE* dataPtr = reinterpret_cast<const BYTE*>(&indata);

	if constexpr (std::is_same_v<T, std::string>)
		dataPtr = reinterpret_cast<const BYTE*>(indata.c_str());
	else if constexpr (std::is_same_v<T, std::wstring>)
		dataPtr = reinterpret_cast<const BYTE*>(indata.c_str());
	else if constexpr (std::is_array_v<T> && (std::is_same_v<std::remove_extent_t<T>, char> || std::is_same_v<std::remove_extent_t<T>, wchar_t>))
		dataPtr = reinterpret_cast<const BYTE*>(indata);

	err = RegSetValueExA(keyval, name, 0, type, dataPtr, static_cast<DWORD>(size));
	RegCloseKey(keyval);

	return err == ERROR_SUCCESS;
}


template<typename T>
static int RegGetKey(HKEY key, LPCSTR keyloc, DWORD type, REGSAM access, LPCSTR name, T& outdata)
{
	HKEY keyval = nullptr;
	LONG err = RegOpenKeyExA(key, keyloc, 0, access, &keyval);
	if (err != ERROR_SUCCESS)
		return 0;

	DWORD dataType = 0;
	DWORD dataSize = 0;
	err = RegQueryValueExA(keyval, name, NULL, &dataType, NULL, &dataSize);
	if (err != ERROR_SUCCESS || dataType != type)
	{
		RegCloseKey(keyval);
		return 0;
	}

	if constexpr (std::is_same_v<T, std::string>)
	{
		char* buffer = new char[dataSize];
		err = RegQueryValueExA(keyval, name, NULL, NULL, reinterpret_cast<BYTE*>(buffer), &dataSize);
		if (err == ERROR_SUCCESS)
			outdata.assign(buffer, dataSize - 1);
		delete[] buffer;
	}
	else if constexpr (std::is_same_v<T, std::wstring>)
	{
		wchar_t* buffer = new wchar_t[dataSize / sizeof(wchar_t)];
		err = RegQueryValueExW(keyval, name, NULL, NULL, reinterpret_cast<BYTE*>(buffer), &dataSize);
		if (err == ERROR_SUCCESS)
			outdata.assign(buffer, (dataSize / sizeof(wchar_t)) - 1);
		delete[] buffer;
	}
	else if constexpr (std::is_array_v<T> && (std::is_same_v<std::remove_extent_t<T>, char> || std::is_same_v<std::remove_extent_t<T>, wchar_t>))
	{
		err = RegQueryValueExA(keyval, name, NULL, NULL, reinterpret_cast<BYTE*>(outdata), &dataSize);
	}
	else
	{
		DWORD expectedSize = sizeof(T);
		if (dataSize != expectedSize)
		{
			RegCloseKey(keyval);
			return 0;
		}
		err = RegQueryValueExA(keyval, name, NULL, NULL, reinterpret_cast<BYTE*>(&outdata), &dataSize);
	}

	RegCloseKey(keyval);

	if (err == ERROR_SUCCESS)
		return 1;
	else if (err == ERROR_FILE_NOT_FOUND)
		return 2;
	else if (err == ERROR_MORE_DATA)
		return 3;
	else
		return 0;
}
template<size_t N>
int RegGetKey(HKEY key, LPCSTR keyloc, DWORD type, REGSAM access, LPCSTR name, char(&outdata)[N]) {
	DWORD size = N;
	HKEY keyval;
	int err = RegOpenKeyExA(key, keyloc, 0, access, &keyval);
	if (err != ERROR_SUCCESS) return 0;

	err = RegQueryValueExA(keyval, name, nullptr, &type, reinterpret_cast<BYTE*>(outdata), &size);
	CloseHandle(keyval);

	switch (err) {
	case ERROR_SUCCESS: return 1;
	case ERROR_FILE_NOT_FOUND: return 2;
	case ERROR_MORE_DATA: return 3;
	default: return 0;
	}
}
template<typename T>
bool RegSetKey(HKEY key, LPCSTR keyloc, DWORD type, REGSAM access, LPCSTR name, const T* indata) {
	DWORD size = sizeof(T);
	HKEY keyval;
	if (RegOpenKeyExA(key, keyloc, 0, access, &keyval) != ERROR_SUCCESS)
		return false;

	bool ok = RegSetValueExA(keyval, name, 0, type, reinterpret_cast<const BYTE*>(indata), size) == ERROR_SUCCESS;
	CloseHandle(keyval);
	return ok;
}
static bool ExtractResourceToFile(HINSTANCE hInstance, LPCWSTR resourceName, LPCWSTR resourceType, const CString& outPath)
{
	HRSRC hRes = FindResourceW(hInstance, resourceName, resourceType);
	if (!hRes) return false;

	DWORD size = SizeofResource(hInstance, hRes);
	HGLOBAL hData = LoadResource(hInstance, hRes);
	if (!hData) return false;

	void* pData = LockResource(hData);
	if (!pData) return false;

	HANDLE hFile = CreateFileW(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return false;

	DWORD written = 0;
	BOOL result = WriteFile(hFile, pData, size, &written, NULL);
	CloseHandle(hFile);
	return result && written == size;
}
