#include "pch.h"
#include "framework.h"
#include "steamcloud.h"
#include "steamcloudDlg.h"
#include "afxdialogex.h"
#include "afxwin.h"
#include "prompt.h"
//#include <thread>
#include <fstream>
#include <chrono>
#include <thread>
#include <Windows.h>

DWORD g_BytesTransferred = 0;
void CsteamcloudDlg::Clearlist()
{
	int count = listfiles->GetItemCount();
	for (int i = 0; i < count; i++)
	{
		
		int test = listfiles->DeleteItem(0);
	}
}

bool CsteamcloudDlg::ReadFromPipeWithTimeout(DWORD timeoutMs, std::string& output)
{
	const DWORD interval = 100; // 100ms polling
	DWORD startTime = GetTickCount64();
	char buffer[8192] = {};
	DWORD totalRead = 0;

	while (GetTickCount64() - startTime < timeoutMs)
	{
		DWORD bytesAvailable = 0;
		if (PeekNamedPipe(m_hPipe, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0)
		{
			DWORD bytesRead = 0;
			if (ReadFile(m_hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
			{
				buffer[bytesRead] = '\0';
				output = buffer;
				return true;
			}
		}
		Sleep(interval);
	}
	return false;
}
void CsteamcloudDlg::GetFiles()
{
	if (!m_hPipe || m_hPipe == INVALID_HANDLE_VALUE) {
		MessageBox(L"Pipe's not open.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	Clearlist();
	m_fileSizesBytes.clear();
	

	// 1. send "list" command
	const char* cmdList = "list\n";
	DWORD written = 0;
	if (!WriteFile(m_hPipe, cmdList, (DWORD)strlen(cmdList), &written, NULL)) {
		MessageBox(L"Unable to write 'list to pipe'", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	std::string listJsonStr;
	if (!ReadFromPipeWithTimeout(10000, listJsonStr)) {
		MessageBox(L"Timeout(10s) when reading result from pipe(list).", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	// 2. Parse JSON listu
	json j;
	try {
		j = json::parse(listJsonStr);
	}
	catch (...) {
		MessageBox(L"Error on parsing JSON result from pipe(list)", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	int index = 0;
	for (auto& [key, val] : j.items()) {
		if (!val.is_object()) continue;

		std::string name = val.value("name", "");
		uint64_t timestamp = val.value("timestamp", 0);
		int size = val.value("size", 0);
		bool exists = val.value("exists", false);
		bool persisted = val.value("persistent", false);
		CString namecs(name.c_str());
		m_fileSizesBytes[namecs] = size;
		CString nameW(name.c_str());
		CString sizeW, dateW;

		// Velikost
		switch (sizeunit) {
		case 0: sizeW.Format(L"%d", size); break;
		case 1: sizeW.Format(L"%.4f", (float)size / 1024); break;
		case 2: sizeW.Format(L"%.4f", (float)size / (1024 * 1024)); break;
		default: sizeW.Format(L"%d", size); break;
		}

		// Datum
		wchar_t dateBuf[100] = {};
		tm* timeinfo = localtime((time_t*)&timestamp);
		wcsftime(dateBuf, sizeof(dateBuf) / sizeof(wchar_t), L"%e.%m.%Y %H:%M:%S", timeinfo);
		dateW = dateBuf;

		listfiles->InsertItem(index, nameW);
		listfiles->SetItemText(index, 1, dateW);
		listfiles->SetItemText(index, 2, sizeW);
		listfiles->SetItemText(index, 3, persisted ? L"true" : L"false");
		listfiles->SetItemText(index, 4, exists ? L"true" : L"false");
		index++;
	}

	// 3. quota
	const char* cmdQuota = "quota\n";
	if (!WriteFile(m_hPipe, cmdQuota, (DWORD)strlen(cmdQuota), &written, NULL)) {
		MessageBox(L"Cannot write the 'quota' command to pipe.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	std::string quotaJsonStr;
	if (!ReadFromPipeWithTimeout(5000, quotaJsonStr)) {
		MessageBox(L"Response read timeout (quota).", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	json quotaJsonArray;
	try {
		quotaJsonArray = json::parse(quotaJsonStr);
	}
	catch (...) {
		MessageBox(L"Error parsing JSON response (quota).", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	if (!quotaJsonArray.is_array() || quotaJsonArray.empty()) {
		MessageBox(L"Invalid quota response format.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	json quotaJson = quotaJsonArray[0];

	uint64_t total = quotaJson.value("total", 0ULL);
	uint64_t used = quotaJson.value("used", 0ULL);
	uint64_t available = quotaJson.value("available", 0ULL);

	m_quotaUsed = used;
	m_quotaTotal = total;
	m_quotaAvailable = available;

	CString quotaText;
	switch (sizeunit) {
	case 0:
		quotaText.Format(L"%llu/%llu Bytes used", used, total);
		break;
	case 1:
		quotaText.Format(L"%.4f/%.4f KB used", (float)used / 1024, (float)total / 1024);
		break;
	case 2:
		quotaText.Format(L"%.4f/%.4f MB used", (float)used / (1024 * 1024), (float)total / (1024 * 1024));
		break;
	default:
		quotaText.Format(L"%llu/%llu Bytes used", used, total);
		break;
	}

	SetDlgItemTextW(IDC_QUOTA, quotaText);
}


VOID CALLBACK FileIOCompletionRoutine(
	__in  DWORD dwErrorCode,
	__in  DWORD dwNumberOfBytesTransfered,
	__in  LPOVERLAPPED lpOverlapped
);
VOID CALLBACK FileIOCompletionRoutine(
	__in  DWORD dwErrorCode,
	__in  DWORD dwNumberOfBytesTransfered,
	__in  LPOVERLAPPED lpOverlapped)
{
	_tprintf(TEXT("Error code:\t%x\n"), dwErrorCode);
	_tprintf(TEXT("Number of bytes:\t%x\n"), dwNumberOfBytesTransfered);
	g_BytesTransferred = dwNumberOfBytesTransfered;
}
#pragma warning( disable:4244)
#pragma warning(disable:6387)
#pragma warning(disable:6011)
#pragma warning(disable:28182)
#pragma warning(disable:4805)

using namespace std;

CsteamcloudDlg::CsteamcloudDlg(CWnd* pParent /*=nullptr*/)
	: CDialog(IDD_STEAMCLOUD_DIALOG, pParent)
{
	m_nidIconData.cbSize = sizeof(NOTIFYICONDATA);
	m_nidIconData.hWnd = 0;
	m_nidIconData.uID = 1;
	m_nidIconData.uCallbackMessage = WM_TRAY_ICON_NOTIFY_MESSAGE;
	m_nidIconData.hIcon = 0;
	m_nidIconData.szTip[0] = 0;
	m_nidIconData.uFlags = NIF_MESSAGE;
	m_bTrayIconVisible = FALSE;
	m_nDefaultMenuItem = 0;
	m_bMinimizeToTray = TRUE;
	m_hIcon = AfxGetApp()->LoadIcon(IDR_ICON);
}
CsteamcloudDlg::~CsteamcloudDlg()
{
	
}

void CsteamcloudDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CsteamcloudDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SYSCOMMAND()
	ON_WM_QUERYDRAGICON()
	ON_MESSAGE(WM_TRAY_ICON_NOTIFY_MESSAGE, OnTrayNotify)
	ON_BN_CLICKED(IDC_TRAYEN, &CsteamcloudDlg::OnBnClickedTrayEn)
	ON_BN_CLICKED(IDC_MINEN, &CsteamcloudDlg::OnBnClickedMinEn)
	ON_BN_CLICKED(IDC_EXIT, &CsteamcloudDlg::OnBnClickedExit)
	ON_COMMAND(ID_MENU_EXIT, &CsteamcloudDlg::OnBnClickedExit)
	ON_COMMAND(ID_MENU_MAX, &CsteamcloudDlg::OnOpen)
	ON_COMMAND(ID_MENU_MIN, &CsteamcloudDlg::OnMinimize)
	ON_BN_CLICKED(IDC_CONNECT, &CsteamcloudDlg::OnBnClickedConnect)
	ON_BN_CLICKED(IDC_DELETE, &CsteamcloudDlg::OnBnClickedDelete)
	ON_BN_CLICKED(IDC_UPLOAD, &CsteamcloudDlg::OnBnClickedUpload)
	ON_BN_CLICKED(IDC_DIRUPLOAD, &CsteamcloudDlg::OnBnClickedDirupload)
	ON_BN_CLICKED(IDC_DOWNLOAD, &CsteamcloudDlg::OnBnClickedDownload)
	ON_BN_CLICKED(IDC_REFRESH, &CsteamcloudDlg::OnBnClickedRefresh)
	ON_BN_CLICKED(IDC_DISCONNECT, &CsteamcloudDlg::OnBnClickedDisconnect)
	ON_BN_CLICKED(IDC_BYTES, &CsteamcloudDlg::OnBnClickedBytes)
	ON_BN_CLICKED(IDC_KBYTES, &CsteamcloudDlg::OnBnClickedKbytes)
	ON_BN_CLICKED(IDC_MBYTES, &CsteamcloudDlg::OnBnClickedMbytes)
END_MESSAGE_MAP()
BOOL CsteamcloudDlg::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	// TODO: Add your specialized code here and/or call the base class
	MSGFILTER* lpMsgFilter = (MSGFILTER*)lParam;

	/*if ((wParam == IDC_MD5OUT) && (lpMsgFilter->nmhdr.code == EN_MSGFILTER)
		&& (lpMsgFilter->msg == WM_RBUTTONDOWN))

	{
		//if we get through here, we have trapped the right click event of the richeditctrl! 
		CPoint point;
		::GetCursorPos(&point); //where is the mouse?
		CMenu menu; //lets display out context menu :) 
		UINT dwSelectionMade;
		VERIFY(menu.LoadMenu(IDR_HASH));
		CMenu* pmenuPopup = menu.GetSubMenu(0);
		ASSERT(pmenuPopup != NULL);
		dwSelectionMade = pmenuPopup->TrackPopupMenu((TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD),
			point.x, point.y, this
		);

		pmenuPopup->DestroyMenu();
		PostMessage(dwSelectionMade, 0, 0);
	}*/

	return CDialog::OnNotify(wParam, lParam, pResult);
}

BOOL CsteamcloudDlg::OnInitDialog()
{

	returned = (wchar_t*)malloc(300);
	int out;
	DWORD indata =  1;
	DWORD outdata = 0;
	BYTE cmp = 1;
	type = REG_DWORD;
	m_hPipe = CreateNamedPipeW(
		L"\\\\.\\pipe\\SteamDlgPipe",
		PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		4096,
		4096,
		0,
		NULL);

	if (m_hPipe == INVALID_HANDLE_VALUE)
	{
		MessageBox(L"Error creating pipe.", L"Error", MB_OK | MB_ICONERROR);
	}
	//AfxInitRichEdit2();
	
	CsteamcloudDlg::ShowWindow(SW_SHOW);
	CsteamcloudDlg::RedrawWindow();
	CsteamcloudDlg::CenterWindow();
	CDialog::OnInitDialog();
	out = RegCrtKey(HKEY_CURRENT_USER, "Software\\steamcloud", KEY_ALL_ACCESS | KEY_WOW64_64KEY);
	if (out == 1)
	{
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayEnable", indata);
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", indata);
		indata = 0;
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "SizeUnit", indata);
		indata = 1;
	}
	else if(out == 0)
	{
		trayenable = 1;
		minimizeen = 1;
		m_bMinimizeToTray = TRUE;
	}
	else if (out == 2)
	{
		out = RegGetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", outdata);
		if (out == 2)
		{
			
			RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", indata);
			
			minimizeen = 1;
			m_bMinimizeToTray = TRUE;
		} else if (out == 1)
		{ 
			
			if (outdata == 1)
			{
				minimizeen = 1;
				m_bMinimizeToTray = TRUE;
			}
			else 
			{
				minimizeen = 0;
				m_bMinimizeToTray = FALSE;
			}
		}
		out = RegGetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayEnable", outdata);
		if (out == 2)
		{

			RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayEnable", indata);
			trayenable = 1;
		}
		else if (out == 1)
		{ 
			if (outdata == 1)
			{
				trayenable = 1;
			}
			else
			{
				trayenable = 0;
			}
		}
		out = RegGetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "SizeUnit", outdata);
		if (out == 2)
		{
			indata = 0;
			RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "SizeUnit", indata);
			sizeunit = 0;
		}
		else if (out == 1)
		{
			sizeunit = outdata;
		}
	}
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	TraySetIcon(m_hIcon);
	TraySetToolTip(L"Steamcloud");
	TraySetMenu(ID_MENU_MINIMIZE);
	
	if (CsteamcloudDlg::IsWindowVisible() != 0)
	{
#ifdef _DEBUG
		//AllocConsole();
#endif
		exitb = (CButton*)GetDlgItem(IDC_EXIT);
		checkbox = (CButton*)GetDlgItem(IDC_MINEN);
		connect = (CButton*)GetDlgItem(IDC_CONNECT);
		trayen = (CButton*)GetDlgItem(IDC_TRAYEN);
		download = (CButton*)GetDlgItem(IDC_DOWNLOAD);
		deletefile = (CButton*)GetDlgItem(IDC_DELETE);
		upload = (CButton*)GetDlgItem(IDC_UPLOAD);
		refresh = (CButton*)GetDlgItem(IDC_REFRESH);
		uploaddir = (CButton*)GetDlgItem(IDC_DIRUPLOAD);
		disconnect = (CButton*)GetDlgItem(IDC_DISCONNECT);
		listfiles = (CListCtrl*)GetDlgItem(IDC_LISTFILES);
		quota = (CStatic*)GetDlgItem(IDC_QUOTA);
		Bytes = (CButton*)GetDlgItem(IDC_BYTES);
		Kbytes = (CButton*)GetDlgItem(IDC_KBYTES);
		Mbytes = (CButton*)GetDlgItem(IDC_MBYTES);
		disconnect = (CButton*)GetDlgItem(IDC_DISCONNECT);
		inputappid = (CComboBox*)GetDlgItem(IDC_INPUTAPPID);
		listfiles->InsertColumn(0, L"Filename",LVCFMT_LEFT,120);
		listfiles->InsertColumn(1, L"TimeStamp", LVCFMT_LEFT, 150);
		listfiles->InsertColumn(2, L"Size (B)", LVCFMT_LEFT, 100);
		listfiles->InsertColumn(3, L"Is Persist", LVCFMT_LEFT, 70);
		listfiles->InsertColumn(4, L"Exist", LVCFMT_LEFT, 70);
		/*download->GetClientRect(&buttonrect);
		buttonrgn.CreateRoundRectRgn(buttonrect.left, buttonrect.top, buttonrect.right, buttonrect.bottom, 22,22);
		download->SetWindowRgn(buttonrgn, TRUE);
		buttonrgn.Detach();
		buttonrgn.DeleteObject();
		deletefile->GetClientRect(&buttonrect);
		buttonrgn.CreateRoundRectRgn(buttonrect.left, buttonrect.top, buttonrect.right, buttonrect.bottom, 22, 22);
		deletefile->SetWindowRgn(buttonrgn, TRUE);
		buttonrgn.Detach();
		buttonrgn.DeleteObject();
		upload->GetClientRect(&buttonrect);
		buttonrgn.CreateRoundRectRgn(buttonrect.left, buttonrect.top, buttonrect.right, buttonrect.bottom, 22, 22);
		upload->SetWindowRgn(buttonrgn, TRUE);
		buttonrgn.Detach();
		buttonrgn.DeleteObject();
		uploaddir->GetClientRect(&buttonrect);
		buttonrgn.CreateRoundRectRgn(buttonrect.left, buttonrect.top, buttonrect.right, buttonrect.bottom, 22, 22);
		uploaddir->SetWindowRgn(buttonrgn, TRUE);
		buttonrgn.Detach();
		buttonrgn.DeleteObject();
		refresh->GetClientRect(&buttonrect);
		buttonrgn.CreateRoundRectRgn(buttonrect.left, buttonrect.top, buttonrect.right, buttonrect.bottom, 22, 22);
		refresh->SetWindowRgn(buttonrgn, TRUE);
		buttonrgn.Detach();
		buttonrgn.DeleteObject();
		connect->GetClientRect(&buttonrect);
		buttonrgn.CreateRoundRectRgn(buttonrect.left, buttonrect.top, buttonrect.right, buttonrect.bottom, 22, 22);
		connect->SetWindowRgn(buttonrgn, TRUE);
		buttonrgn.Detach();
		buttonrgn.DeleteObject();
		disconnect->GetClientRect(&buttonrect);
		buttonrgn.CreateRoundRectRgn(buttonrect.left, buttonrect.top, buttonrect.right, buttonrect.bottom, 22, 22);
		disconnect->SetWindowRgn(buttonrgn, TRUE);
		buttonrgn.Detach();
		buttonrgn.DeleteObject();
		exitb->GetClientRect(&buttonrect);
		buttonrgn.CreateRoundRectRgn(buttonrect.left, buttonrect.top, buttonrect.right, buttonrect.bottom, 100, 100);
		exitb->SetWindowRgn(buttonrgn, TRUE);
		buttonrgn.Detach();
		//buttonrgn.DeleteObject();*/ //maybe round buttons on next update
		download->EnableWindow(0);
		deletefile->EnableWindow(0);
		upload->EnableWindow(0);
		uploaddir->EnableWindow(0);
		refresh->EnableWindow(0);
		quota->ShowWindow(0);
		disconnect->EnableWindow(0);
		if (minimizeen)
		{
			CheckDlgButton(IDC_MINEN, BST_CHECKED);
		}
		else
		{
			CheckDlgButton(IDC_MINEN, BST_UNCHECKED);
		}
		if (trayenable)
		{
			CheckDlgButton(IDC_TRAYEN, BST_CHECKED);
			TrayShow();
		}
		else
		{
			CheckDlgButton(IDC_TRAYEN, BST_UNCHECKED);
			TrayHide();
		}
		switch (sizeunit)
		{
		case 0:
		{
			Bytes->SetCheck(BST_CHECKED);
			Kbytes->SetCheck(BST_UNCHECKED);
			Mbytes->SetCheck(BST_UNCHECKED);
			LVCOLUMN col;
			memset(&col, 0, sizeof(col));
			col.mask = LVCF_TEXT;
			listfiles->GetColumn(2, &col);
			col.pszText = L"Size (B)";
			listfiles->SetColumn(2, &col);
			break;
		}
		case 1:
		{
			Bytes->SetCheck(BST_UNCHECKED);
			Kbytes->SetCheck(BST_CHECKED);
			Mbytes->SetCheck(BST_UNCHECKED);
			LVCOLUMN col;
			memset(&col, 0, sizeof(col));
			col.mask = LVCF_TEXT;
			listfiles->GetColumn(2, &col);
			col.pszText = L"Size (KB)";
			listfiles->SetColumn(2, &col);
			break;
		}
		case 2:
		{
			Bytes->SetCheck(BST_UNCHECKED);
			Kbytes->SetCheck(BST_UNCHECKED);
			Mbytes->SetCheck(BST_CHECKED);
			LVCOLUMN col;
			memset(&col, 0, sizeof(col));
			col.mask = LVCF_TEXT;
			listfiles->GetColumn(2, &col);
			col.pszText = L"Size (MB)";
			listfiles->SetColumn(2, &col);
			break;
		}
		default:
		{
			sizeunit = 0;
			Bytes->SetCheck(BST_CHECKED);
			Kbytes->SetCheck(BST_UNCHECKED);
			Mbytes->SetCheck(BST_UNCHECKED);
			LVCOLUMN col;
			memset(&col, 0, sizeof(col));
			col.mask = LVCF_TEXT;
			listfiles->GetColumn(2, &col);
			col.pszText = L"Size (B)";
			listfiles->SetColumn(2, &col);
			break;
		}
		}
		LoadComboBoxHistory();
	}
	
	return TRUE;
}
void CsteamcloudDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
	CsteamcloudDlg::ShowWindow(SW_SHOW);
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CsteamcloudDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CsteamcloudDlg::OnBnClickedExit()
{
	CsteamcloudDlg::OnDestroy();
	PostQuitMessage(1);
}

void CsteamcloudDlg::OnBnClickedMinEn()
{
	DWORD indata = 0;
	int boxcheck = checkbox->GetCheck();
	if (boxcheck == BST_CHECKED)
	{
		indata = 1;
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", indata);
		m_bMinimizeToTray = TRUE;
		minimizeen = true;
	}
	else
	{
		indata = 0;
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", indata);
		m_bMinimizeToTray = FALSE;
		minimizeen = false;
	}
}
void CsteamcloudDlg::OnBnClickedTrayEn()
{
	int tren;
	DWORD indata = 0;
	tren = trayen->GetCheck();
	if (tren == BST_CHECKED)
	{
		indata = 1;
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayEnable", indata);
		trayenable = true;
		checkbox->EnableWindow();
		TrayShow();
	}
	else
	{
		indata = 0;
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayEnable", indata);
		trayenable = false;
		m_bMinimizeToTray = FALSE;
		checkbox->EnableWindow(0);
		TrayHide();
	}
}
void CsteamcloudDlg::OnOpen()
{
	if (this->IsWindowVisible())
	{ 
		this->ShowWindow(SW_NORMAL);
		this->SetFocus();
	}
	else
	{	
		this->ShowWindow(SW_SHOW);
		this->SetFocus();
	}
	
	
}

int CsteamcloudDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDialog::OnCreate(lpCreateStruct) == -1)
		return -1;

	m_nidIconData.hWnd = this->m_hWnd;
	m_nidIconData.uID = 1;

	return 0;
}

void CsteamcloudDlg::OnDestroy()
{
	if (init)
	{
		if (m_hWorkerProcess && m_hPipe)
		{
			const char* exitCmd = "exit\n";
			DWORD bytesWritten = 0;
			if (WriteFile(m_hPipe, exitCmd, (DWORD)strlen(exitCmd), &bytesWritten, NULL))
			{
				Sleep(200); // Wait for the worker process to handle the exit command

				DWORD result = WaitForSingleObject(m_hWorkerProcess, 0);
				if (result == WAIT_TIMEOUT)
				{
					// The worker process is still running, terminate it
					TerminateProcess(m_hWorkerProcess, 1);
				}
			}
		}
		init = false;
	}
	if (m_hPipe && m_hPipe != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hPipe);
		m_hPipe = NULL;
	}

	CDialog::OnDestroy();
	if (m_nidIconData.hWnd && m_nidIconData.uID > 0 && TrayIsVisible())
	{
		Shell_NotifyIcon(NIM_DELETE, &m_nidIconData);
	}
}

BOOL CsteamcloudDlg::TrayIsVisible()
{
	return m_bTrayIconVisible;
}

void CsteamcloudDlg::TraySetIcon(HICON hIcon)
{
	ASSERT(hIcon);

	m_nidIconData.hIcon = hIcon;
	m_nidIconData.uFlags |= NIF_ICON;
}

void CsteamcloudDlg::TraySetIcon(UINT nResourceID)
{
	ASSERT(nResourceID > 0);
	HICON hIcon = 0;
	hIcon = AfxGetApp()->LoadIcon(nResourceID);
	if (hIcon)
	{
		m_nidIconData.hIcon = hIcon;
		m_nidIconData.uFlags |= NIF_ICON;
	}
	else
	{
		TRACE0("FAILED TO LOAD ICON\n");
	}
}

void CsteamcloudDlg::TraySetIcon(LPCTSTR lpszResourceName)
{
	HICON hIcon = 0;
	hIcon = AfxGetApp()->LoadIcon(lpszResourceName);
	if (hIcon)
	{
		m_nidIconData.hIcon = hIcon;
		m_nidIconData.uFlags |= NIF_ICON;
	}
	else
	{
		TRACE0("FAILED TO LOAD ICON\n");
	}
}

void CsteamcloudDlg::TraySetToolTip(LPCTSTR lpszToolTip)
{
	ASSERT(strlen((char*)lpszToolTip) > 0 && strlen((char*)lpszToolTip) < 64);

	wcscpy_s(m_nidIconData.szTip,lpszToolTip);
	m_nidIconData.uFlags |= NIF_TIP;
}

BOOL CsteamcloudDlg::TrayShow()
{
	BOOL bSuccess = FALSE;
	if (!m_bTrayIconVisible && trayenable)
	{
		bSuccess = Shell_NotifyIcon(NIM_ADD, &m_nidIconData);
		if (bSuccess)
			m_bTrayIconVisible = TRUE;
	}
	else
	{
		TRACE0("ICON ALREADY VISIBLE");
	}
	return bSuccess;
}

BOOL CsteamcloudDlg::TrayHide()
{
	BOOL bSuccess = FALSE;
	if (m_bTrayIconVisible)
	{
		bSuccess = Shell_NotifyIcon(NIM_DELETE, &m_nidIconData);
		if (bSuccess)
			m_bTrayIconVisible = FALSE;
	}
	else
	{
		TRACE0("ICON ALREADY HIDDEN");
	}
	bSuccess = TRUE;
	return bSuccess;
}

BOOL CsteamcloudDlg::TrayUpdate()
{
	BOOL bSuccess = FALSE;
	if (m_bTrayIconVisible)
	{
		bSuccess = Shell_NotifyIcon(NIM_MODIFY, &m_nidIconData);
	}
	else
	{
		TRACE0("ICON NOT VISIBLE");
	}
	return bSuccess;
}


BOOL CsteamcloudDlg::TraySetMenu(UINT nResourceID, UINT nDefaultPos)
{
	BOOL bSuccess;
	bSuccess = m_mnuTrayMenu.LoadMenu(nResourceID);
	return bSuccess;
}


BOOL CsteamcloudDlg::TraySetMenu(LPCTSTR lpszMenuName, UINT nDefaultPos)
{
	BOOL bSuccess;
	bSuccess = m_mnuTrayMenu.LoadMenu(lpszMenuName);
	return bSuccess;
}

BOOL CsteamcloudDlg::TraySetMenu(HMENU hMenu, UINT nDefaultPos)
{
	m_mnuTrayMenu.Attach(hMenu);
	return TRUE;
}

LRESULT CsteamcloudDlg::OnTrayNotify(WPARAM wParam, LPARAM lParam)
{
	UINT uID;
	UINT uMsg;

	uID = (UINT)wParam;
	uMsg = (UINT)lParam;

	if (uID != 1)
		return false;

	CPoint pt;

	switch (uMsg)
	{
	case WM_MOUSEMOVE:
		GetCursorPos(&pt);
		ClientToScreen(&pt);
		OnTrayMouseMove(pt);
		break;
	case WM_LBUTTONDOWN:
		GetCursorPos(&pt);
		ClientToScreen(&pt);
		OnTrayLButtonDown(pt);
		break;
	case WM_LBUTTONDBLCLK:
		GetCursorPos(&pt);
		ClientToScreen(&pt);
		OnTrayLButtonDblClk(pt);
		break;

	case WM_RBUTTONDOWN:
	case WM_CONTEXTMENU:
		GetCursorPos(&pt);
		//ClientToScreen(&pt);
		OnTrayRButtonDown(pt);
		break;
	case WM_RBUTTONDBLCLK:
		GetCursorPos(&pt);
		ClientToScreen(&pt);
		OnTrayRButtonDblClk(pt);
		break;
	}
	return true;
}
void CsteamcloudDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if (m_bMinimizeToTray)
	{
		if ((nID & 0xFFF0) == SC_MINIMIZE)
		{
		
			if (TrayShow() || m_bTrayIconVisible)
			{
				this->ShowWindow(SW_HIDE);
			}
		}
		else
		{
			CDialog::OnSysCommand(nID, lParam);
		}
	}
	else
	{ 
		CDialog::OnSysCommand(nID, lParam);
	}
}
void CsteamcloudDlg::TraySetMinimizeToTray(BOOL bMinimizeToTray)
{
	m_bMinimizeToTray = bMinimizeToTray;
}


void CsteamcloudDlg::OnTrayRButtonDown(CPoint pt)
{
	m_mnuTrayMenu.GetSubMenu(0)->TrackPopupMenu(TPM_BOTTOMALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON, pt.x, pt.y, this);
	m_mnuTrayMenu.GetSubMenu(0)->SetDefaultItem(m_nDefaultMenuItem, TRUE);
}

void CsteamcloudDlg::OnTrayLButtonDown(CPoint pt)
{

}

void CsteamcloudDlg::OnTrayLButtonDblClk(CPoint pt)
{
	if (m_bMinimizeToTray)
	{


		this->ShowWindow(SW_SHOW);
		this->ShowWindow(SW_MAXIMIZE);
		this->SetFocus();
	}
}

void CsteamcloudDlg::OnTrayRButtonDblClk(CPoint pt)
{
}

void CsteamcloudDlg::OnTrayMouseMove(CPoint pt)
{
}
void CsteamcloudDlg::OnMinimize()
{
	if(this->IsWindowVisible())
	{ 
		if (m_bMinimizeToTray && trayenable)
		{
			if (minimizeen)
			{
				ShowWindow(SW_HIDE);
				ShowWindow(SW_MINIMIZE);
			}
		}	
	}
}

PCHAR* CsteamcloudDlg::CommandLineToArgvA(PCHAR CmdLine,int* _argc)
{
	PCHAR* argv;
	PCHAR  _argv;
	ULONG   len;
	ULONG   argc;
	CHAR   a;
	ULONG   i, j;

	BOOLEAN  in_QM;
	BOOLEAN  in_TEXT;
	BOOLEAN  in_SPACE;

	len = (ULONG)strlen(CmdLine);
	i = ((len + 2) / 2) * sizeof(PVOID) + sizeof(PVOID);

	argv = (PCHAR*)GlobalAlloc(GMEM_FIXED,
		i + (len + 2) * sizeof(CHAR));

	_argv = (PCHAR)(((PUCHAR)argv) + i);

	argc = 0;
	argv[argc] = _argv;
	in_QM = FALSE;
	in_TEXT = FALSE;
	in_SPACE = TRUE;
	i = 0;
	j = 0;

	while (a = CmdLine[i]) {
		if (in_QM) {
			if (a == '\"') {
				in_QM = FALSE;
			}
			else {
				_argv[j] = a;
				j++;
			}
		}
		else {
			switch (a) {
			case '\"':
				in_QM = TRUE;
				in_TEXT = TRUE;
				if (in_SPACE) {
					argv[argc] = _argv + j;
					argc++;
				}
				in_SPACE = FALSE;
				break;
			case ' ':
			case '\t':
			case '\n':
			case '\r':
				if (in_TEXT) {
					_argv[j] = '\0';
					j++;
				}
				in_TEXT = FALSE;
				in_SPACE = TRUE;
				break;
			default:
				in_TEXT = TRUE;
				if (in_SPACE) {
					argv[argc] = _argv + j;
					argc++;
				}
				_argv[j] = a;
				j++;
				in_SPACE = FALSE;
				break;
			}
		}
		i++;
	}
	_argv[j] = '\0';
	argv[argc] = NULL;

	(*_argc) = argc;
	return argv;
}

void CsteamcloudDlg::OnBnClickedConnect()
{
	Clearlist();
	// Získání a validace AppID
	CString appidStr;
	inputappid->GetWindowTextW(appidStr);
	appidStr.Trim();
	if (appidStr.IsEmpty()) {
		MessageBox(L"App ID is empty, please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	for (int i = 0; i < appidStr.GetLength(); ++i) {
		if (!isdigit(appidStr[i])) {
			MessageBox(L"App ID must be a number!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
			return;
		}
	}
	int appid = _wtoi(appidStr);
	if (appid < 1) {
		MessageBox(L"App ID must be greater than 0!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	// If the worker process is not running, start it
	if (m_hWorkerProcess == NULL)
	{
		// Get TEMP path
		WCHAR tempPath[MAX_PATH];
		if (!GetEnvironmentVariableW(L"TEMP", tempPath, MAX_PATH)) {
			MessageBox(L"Cannot get TEMP path.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
			return;
		}
		CString workerPath = CString(tempPath) + L"\\steam-worker.exe";
		CString dllPath;
#ifdef _WIN64
		dllPath = CString(tempPath) + L"\\steam_api64.dll";
#else
		dllPath = CString(tempPath) + L"\\steam_api.dll";
#endif

		// Resource extraction
		if (!PathFileExistsW(workerPath)) {
			DeleteFileW(workerPath); // Odstranění předchozí verze, pokud existuje
		}
		if (!ExtractResourceToFile(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_WORKER), RT_RCDATA, workerPath)) {
			MessageBox(L"Unable to extract steam-worker.exe!", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
			return;
		}
		if (!PathFileExistsW(dllPath)) {
			DeleteFileW(dllPath); // Remove Previous Version if exists - needed when steam-worker.exe is updated
		}
		if (!ExtractResourceToFile(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_STEAMDLL), RT_RCDATA, dllPath)) {
			MessageBox(L"Cannot extract steam_api DLL!", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
			return;
		}

		// Running the worker process
		PROCESS_INFORMATION pi;
		STARTUPINFOW si = { sizeof(si) };
		if (!CreateProcessW(workerPath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
			MessageBox(L"Failed to start steam-worker.exe", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
			return;
		}
		m_hWorkerProcess = pi.hProcess;
		CloseHandle(pi.hThread);

		// Wait for the worker to send "READY" message - this is crucial to ensure the worker is ready before sending any commands
		char buffer[1024];
		DWORD bytesRead = 0;
		bool readyReceived = false;
		const DWORD timeoutMs = 10000;
		DWORD startTime = GetTickCount64();

		while (GetTickCount64() - startTime < timeoutMs)
		{
			DWORD bytesAvailable = 0;
			if (PeekNamedPipe(m_hPipe, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0)
			{
				if (ReadFile(m_hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
				{
					buffer[bytesRead] = '\0';
					if (strcmp(buffer, "READY") == 0)
					{
						readyReceived = true;
						break;
					}
					else
					{
						MessageBox(L"Worker doesn't sended READY", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
						TerminateProcess(m_hWorkerProcess, 1);
						CloseHandle(m_hWorkerProcess);
						m_hWorkerProcess = NULL;
						return;
					}
				}
			}
			Sleep(100);
		}
		// If we reach here and readyReceived is still false, it means we timed out waiting for READY
		if (!readyReceived)
		{
			MessageBox(L"Worker doesn't sended READY in 10s.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
			TerminateProcess(m_hWorkerProcess, 1);
			CloseHandle(m_hWorkerProcess);
			m_hWorkerProcess = NULL;
			return;
		}
	}

	// Send the connect command to the worker with the specified AppID
	std::string cmd = "connect " + std::to_string(appid) + "\n";
	DWORD bytesWritten = 0;
	if (!WriteFile(m_hPipe, cmd.c_str(), (DWORD)cmd.length(), &bytesWritten, NULL)) {
		MessageBox(L"Can't write to pipe.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	// Wait for a response from the worker within 5seconds
	char response[4096];
	DWORD bytesRead = 0;
	const DWORD replyTimeoutMs = 5000;
	DWORD replyStart = GetTickCount64();
	bool gotReply = false;

	while (GetTickCount64() - replyStart < replyTimeoutMs)
	{
		DWORD bytesAvailable = 0;
		if (PeekNamedPipe(m_hPipe, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0)
		{
			if (ReadFile(m_hPipe, response, sizeof(response) - 1, &bytesRead, NULL) && bytesRead > 0)
			{
				response[bytesRead] = '\0';
				gotReply = true;
				break;
			}
		}
		Sleep(100);
	}
	// If we reach here and gotReply is still false, it means we timed out waiting for a reply
	if (!gotReply)
	{
		MessageBox(L"No response from worker", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	try {
		auto parsed = json::parse(response);
		if (parsed.is_array() && parsed.size() > 0 && parsed[0]["status"] == "connected") {
			UpdateAppIdHistoryFromInput();
			SaveComboBoxHistory();
			Clearlist();
			GetFiles(); // Get files after successful connection
			deletefile->EnableWindow();
			upload->EnableWindow();
			uploaddir->EnableWindow();
			download->EnableWindow();
			refresh->EnableWindow();
			quota->ShowWindow(1);
			disconnect->EnableWindow();
			init = true;
			//MessageBox(L"Successfull connection", L"Info", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
		}
		else {
			CString jsonStr(response);
			MessageBox(jsonStr, L"Worker Response", MB_OK | MB_ICONERROR | MB_TOPMOST);
		}
	}
	catch (const std::exception& e) {
		CString msg;
		msg.Format(L"Error when parsing JSON: %S", e.what());
		MessageBox(msg, L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
	}
}


void CsteamcloudDlg::OnBnClickedDelete()
{
	int files[25] = { -1 };
	fill_n(files, 25, -1);
	int count = 0;
	int mark = -1;
	POSITION pos = listfiles->GetFirstSelectedItemPosition();
	if (pos == NULL)
	{
		MessageBox(L"No file is selected! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	mark = listfiles->GetNextSelectedItem(pos);
	while (mark != -1)
	{
		if (count == 25) break;
		files[count++] = mark;
		mark = listfiles->GetNextSelectedItem(pos);
	}

	// Making delete command
	std::ostringstream cmd;
	cmd << "delete ";
	for (int i = 0; i < count; ++i)
	{
		CString nameW = listfiles->GetItemText(files[i], 0);
		CT2A nameA(nameW);
		cmd << nameA;
		if (i != count - 1) cmd << ";";
	}

	std::string command = cmd.str();
	DWORD written = 0;
	if (!WriteFile(m_hPipe, command.c_str(), (DWORD)command.size(), &written, NULL))
	{
		MessageBox(L"Failed to send delete command to worker!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	// waiting for response from worker
	std::string output;
	if (!ReadFromPipeWithTimeout(5000, output))
	{
		MessageBox(L"Timeout while waiting for delete response!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	// Parsing JSON
	using json = nlohmann::json;
	CString result;
	try {
		auto j = json::parse(output);
		for (auto& entry : j)
		{
			std::string name = entry.value("name", "");
			bool deleted = entry.value("deleted", false);
			CString line;
			line.Format(L"%S: %s\n", name.c_str(), deleted ? L"Deleted" : L"Failed");
			result += line;
		}
	}
	catch (std::exception& e) {
		CString err;
		err.Format(L"Failed to parse JSON: %S", e.what());
		MessageBox(err, L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	MessageBox(result, L"Delete result", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);

	Clearlist();
	GetFiles();
}


void CsteamcloudDlg::OnBnClickedUpload()
{
	bool fileopened[MAX_FILES_COUNT] = { false };
	bool filesizeok[MAX_FILES_COUNT] = { false };
	wchar_t filelist[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t filename[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t dirname[MAX_PATH] = { 0 };
	int filecount = 0;
	CString filenames;
	DWORD maxfiles = 10;
	fstream file;

	// Výběr souborů
	CFileDialog dlg(TRUE, NULL, NULL,
		OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST,
		L"All Files (*.*)|*.*||", this);
	OPENFILENAMEW& ofn = dlg.GetOFN();
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = this->m_hWnd;

	DWORD size = MAX_UNICODE_PATH + (MAX_PATH * MAX_FILES_COUNT);
	filenames.GetBufferSetLength(size);
	ofn.lpstrFile = filenames.GetBuffer();
	ofn.nMaxFile = size;

	if (dlg.DoModal() != IDOK) return;

	// Získání cesty a jmen souborů
	int pos = -1;
	pos = FindPosition(filenames.GetBuffer(), pos);
	wcscpy_s(dirname, filenames.GetString());

	for (UINT i = 0; i < MAX_FILES_COUNT; i++)
	{
		int prevpos = pos;
		pos = FindPosition(filenames.GetBuffer(), pos);
		if (pos == prevpos || pos == prevpos + 1 || pos == -1)
		{
			if (i == 0)
			{
				filecount++;
				wcscpy_s(filelist[i], dirname);
				wcscpy_s(filename[i], dlg.GetFileName().GetString());
				wcscpy_s(dirname, dlg.GetFolderPath().GetString());
			}
			break;
		}
		filecount++;
		swprintf_s(filelist[i], L"%s\\%s", dirname, &filenames.GetBuffer()[prevpos + 1]);
		swprintf_s(filename[i], L"%s", &filenames.GetBuffer()[prevpos + 1]);
	}

	// Sestavení příkazu upload cloudname,localpath;...
	std::ostringstream uploadCommand;
	uploadCommand << "upload ";

	for (int i = 0; i < filecount; ++i)
	{
		file.open(filelist[i], ios::in | ios::binary);
		if (file.is_open())
		{
			fileopened[i] = true;
			file.seekg(0, std::ios::end);
			ULONG length = (ULONG)file.tellg();
			file.seekg(0, std::ios::beg);

			if (length >= 102400000)
			{
				filesizeok[i] = false;
				file.close();
				continue;
			}
			filesizeok[i] = true;
			char cloudName[MAX_PATH];
			wcstombs(cloudName, filename[i], sizeof(cloudName));

			char localPath[MAX_PATH];
			wcstombs(localPath, filelist[i], sizeof(localPath));

			uploadCommand << cloudName << "," << localPath;
			if (i < filecount - 1)
				uploadCommand << ";";

			file.close();
		}
	}

	// Odeslat do pipe
	std::string commandStr = uploadCommand.str();
	DWORD bytesWritten = 0;
	if (!WriteFile(m_hPipe, commandStr.c_str(), (DWORD)commandStr.size(), &bytesWritten, NULL))
	{
		MessageBox(L"Unable to write to pipe!", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	// Čekání na odpověď
	std::string output;
	if (!ReadFromPipeWithTimeout(10000, output))
	{
		MessageBox(L"Unable to read response from pipe.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	// Parsuj JSON
	using json = nlohmann::json;
	CString msg;
	try {
		auto j = json::parse(output);
		for (const auto& entry : j)
		{
			if (entry.contains("error"))
			{
				msg += CString(entry["name"].get<std::string>().c_str()) + L": " +
					CString(entry["error"].get<std::string>().c_str()) + L"\r\n";
			}
			else
			{
				msg += CString(entry["name"].get<std::string>().c_str()) + L": uploaded (" +
					std::to_wstring(entry["size"].get<int>()).c_str() + L" bytes)\r\n";
			}
		}
	}
	catch (...) {
		MessageBox(L"Answer from worker is not valid JSON!", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	MessageBox(msg, L"Upload results", MB_OK | MB_TOPMOST);

	Clearlist();
	GetFiles();
}



void CsteamcloudDlg::OnBnClickedDirupload()
{
	bool emptydir = true;
	prompt dialog(this);
	wchar_t filelist[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t filename[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t dirname[MAX_PATH] = { 0 };
	wchar_t dirupload[MAX_PATH] = { 0 };
	int filecount = 0;
	CString filenames;
	DWORD maxfiles = 10;

	wchar_t* pFile = nullptr;
	wchar_t szFilter[] = L"All Files (*.*)|*.*||";
	CFileDialog dlg(TRUE, NULL, NULL,
		OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
		OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST, szFilter, this);
	OPENFILENAMEW& ofn = dlg.GetOFN();
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = this->m_hWnd;

	DWORD size = MAX_UNICODE_PATH + (MAX_PATH * MAX_FILES_COUNT);
	filenames.GetBufferSetLength(size);
	ofn.lpstrFile = filenames.GetBuffer();
	ofn.nMaxFile = size;

	if (dialog.DoModal() == IDOK)
	{
		if (wcscmp(returned, L"") != 0)
		{
			wcscpy_s(dirupload, returned);
			emptydir = true;

			// Převést \ na /
			for (int i = 0; dirupload[i]; ++i)
			{
				if (dirupload[i] == L'\\')
					dirupload[i] = L'/';
			}

			// Odebrat koncové /
			size_t len = wcslen(dirupload);
			while (len > 0 && dirupload[len - 1] == L'/')
			{
				dirupload[len - 1] = L'\0';
				--len;
			}
		}
		else
		{
			wcscpy_s(dirupload, L"");
			emptydir = false;
		}
		if (dlg.DoModal() == IDOK)
		{
			int pos = -1;
			pos = FindPosition(filenames.GetBuffer(), pos);
			wcscpy_s(dirname, filenames.GetString());

			for (UINT i = 0; i < MAX_FILES_COUNT; i++)
			{
				int prevpos = pos;
				pos = FindPosition(filenames.GetBuffer(), pos);
				if (pos == prevpos || pos == prevpos + 1 || pos == -1)
				{
					if (i == 0)
					{
						filecount++;
						wcscpy_s(filelist[i], dirname);
						memset(dirname, 0, sizeof(dirname));
						wcscpy_s(filename[i], dlg.GetFileName().GetString());
						if (wcslen(dirupload) > 0)
						{
							wchar_t tmp[MAX_PATH];
							swprintf_s(tmp, L"%s/%s", dirupload, filename[i]);
							wcscpy_s(filename[i], tmp);
							swprintf_s(filelist[i], L"%s\\%s", dlg.GetFolderPath().GetString(), dlg.GetFileName().GetString());
						}
						else
						{
							wcscpy_s(dirname, dlg.GetFolderPath().GetString());
						}
					}
					break;
				}
				filecount++;
				if (wcslen(dirupload) > 0)
					swprintf_s(filename[i], L"%s/%s", dirupload, &filenames.GetBuffer()[prevpos + 1]);
				else
					swprintf_s(filename[i], L"%s", &filenames.GetBuffer()[prevpos + 1]);

				swprintf_s(filelist[i], L"%s\\%s", dirname, &filenames.GetBuffer()[prevpos + 1]);
			}

			// vytvoření příkazu upload
			std::ostringstream uploadCommand;
			uploadCommand << "upload ";
			for (int i = 0; i < filecount; i++)
			{
				if (i > 0) uploadCommand << ";";
				CT2A cloudName(filename[i]);
				CT2A localPath(filelist[i]);
				uploadCommand << cloudName.m_psz << "," << localPath.m_psz;
			}

			DWORD written;
			std::string commandStr = uploadCommand.str();
			WriteFile(m_hPipe, commandStr.c_str(), (DWORD)commandStr.size(), &written, NULL);

			std::string response;
			if (!ReadFromPipeWithTimeout(5000, response))
			{
				MessageBox(L"Timeout while uploading files", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
				return;
			}

			try
			{
				json parsed = json::parse(response);
				std::wstring output;
				for (auto& entry : parsed)
				{
					std::wstring name = CA2W(entry["name"].get<std::string>().c_str());
					if (entry.contains("status") && entry["status"] == "uploaded")
					{
						output += L"✔️ ";
					}
					else if (entry.contains("error"))
					{
						output += L"❌ ";
					}
					output += name;

					if (entry.contains("error"))
					{
						output += L" - ";
						output += CA2W(entry["error"].get<std::string>().c_str());
					}
					output += L"\n";
				}
				MessageBox(output.c_str(), L"Upload result", MB_OK | MB_TOPMOST);
			}
			catch (...)
			{
				MessageBox(L"Failed to parse response", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
			}
		}
	}

	Clearlist();
	GetFiles();
	Sleep(1);
}

void CsteamcloudDlg::OnBnClickedDownload()
{
	constexpr int MAX_SELECTED = 10;
	int selectedIndices[MAX_SELECTED];
	int count = 0;

	// Získání vybraných řádků
	POSITION pos = listfiles->GetFirstSelectedItemPosition();
	while (pos && count < MAX_SELECTED)
	{
		int index = listfiles->GetNextSelectedItem(pos);
		selectedIndices[count++] = index;
	}

	if (count == 0)
	{
		MessageBox(L"No file is selected! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	std::vector<CString> cloudPaths;
	std::vector<CString> outputPaths;
	CString targetDirectory;

	// If only one file is selected, prefill filename and ask for output path
	if (count == 1)
	{
		CString cloudPath = listfiles->GetItemText(selectedIndices[0], 0);
		CString fileName = cloudPath.Mid(cloudPath.ReverseFind(L'/') + 1);

		CFileDialog filedialog(FALSE, NULL, fileName, OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, L"All Files (*.*)|*.*||");
		if (filedialog.DoModal() != IDOK)
			return;

		CString fullOutput = filedialog.GetPathName();
		cloudPaths.push_back(cloudPath);
		outputPaths.push_back(fullOutput);
	}
	else
	{
		// If multiple files are selected, ask for target directory
		CFolderPickerDialog dialog(NULL, OFN_EXPLORER | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST | OFN_CREATEPROMPT, this);
		if (dialog.DoModal() != IDOK)
			return;

		targetDirectory = dialog.GetFolderPath();

		for (int i = 0; i < count; ++i)
		{
			CString cloudPath = listfiles->GetItemText(selectedIndices[i], 0);
			CString fileName = cloudPath.Mid(cloudPath.ReverseFind(L'/') + 1);
			CString fullPath = targetDirectory + L"\\" + fileName;

			cloudPaths.push_back(cloudPath);
			outputPaths.push_back(fullPath);
		}
	}

	// making download command with args
	std::stringstream ss;
	ss << "download ";
	for (int i = 0; i < count; ++i)
	{
		CT2A path(cloudPaths[i]);
		ss << path;
		if (i < count - 1)
			ss << ";";
	}
	ss << ";";
	if (count == 1)
	{
		CT2A output(outputPaths[0]);
		ss << output;
	}
	else
	{
		CT2A output(targetDirectory);
		ss << output;
	}
	std::string cmd = ss.str();

	// sending command to worker
	DWORD bytesWritten = 0;
	if (!WriteFile(m_hPipe, cmd.c_str(), (DWORD)cmd.length(), &bytesWritten, NULL))
	{
		MessageBox(L"Unable to write to pipe", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	// Čtení odpovědi
	std::string response;
	if (!ReadFromPipeWithTimeout(10000, response))
	{
		MessageBox(L"Unable to get answer from worker.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	//ss
	try
	{
		using json = nlohmann::json;
		json result = json::parse(response);

		CString statusMsg;
		for (const auto& item : result)
		{
			std::string name = item.value("name", "");
			std::string status = item.value("status", "");
			int size = item.value("size", 0);

			CString line;
			line.Format(L"%S - %S - %d bytes\n", name.c_str(), status.c_str(), size);
			statusMsg += line;
		}

		MessageBox(statusMsg, L"Download status", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
	}
	catch (...)
	{
		MessageBox(L"Error on parsing JSON response.", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
	}
}



void CsteamcloudDlg::OnBnClickedRefresh()
{
	Clearlist();
	GetFiles();
}

void CsteamcloudDlg::OnBnClickedDisconnect()
{
	if (!init) return; // If not initialized, do nothing
	// Attempt to send exit command to worker process
	if (m_hWorkerProcess && m_hPipe)
	{
		const char* exitCmd = "exit\n";
		DWORD bytesWritten = 0;
		if (!WriteFile(m_hPipe, exitCmd, (DWORD)strlen(exitCmd), &bytesWritten, NULL)) {
			MessageBox(L"Unable to write to pipe (exit).", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		}
		else
		{
			Sleep(200); // Wait for a short time to allow the worker to process the exit command

			DWORD result = WaitForSingleObject(m_hWorkerProcess, 0);
			if (result == WAIT_TIMEOUT)
			{
				// If the worker process is still running, we will try to terminate it
				if (!TerminateProcess(m_hWorkerProcess, 1)) {
					MessageBox(L"The worker process cannot be terminated manually.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
				}
				else {
					MessageBox(L"Worker was terminated manually. Disconnected", L"Info", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
					init = false;
				}
			}
			else {
				MessageBox(L"Worker has ended successfully. Disconnected.", L"Info", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
				init = false;
			}
		}
	}

	// Zavření handle
	if (m_hWorkerProcess)
	{
		CloseHandle(m_hWorkerProcess);
		m_hWorkerProcess = NULL;
	}

	// Ukončení Steam a reset GUI
	download->EnableWindow(0);
	deletefile->EnableWindow(0);
	upload->EnableWindow(0);
	uploaddir->EnableWindow(0);
	refresh->EnableWindow(0);
	quota->ShowWindow(0);
	disconnect->EnableWindow(0);
	Clearlist();
}


void CsteamcloudDlg::OnBnClickedBytes()
{
	sizeunit = 0;
	RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "SizeUnit", sizeunit);
	Bytes->SetCheck(BST_CHECKED);
	Kbytes->SetCheck(BST_UNCHECKED);
	Mbytes->SetCheck(BST_UNCHECKED);
	LVCOLUMN col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT;
	listfiles->GetColumn(2, &col);
	col.pszText = L"Size (B)";
	listfiles->SetColumn(2, &col);
	UpdateFileSizesDisplay();
}


void CsteamcloudDlg::OnBnClickedKbytes()
{
	sizeunit = 1;
	RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "SizeUnit", sizeunit);
	Bytes->SetCheck(BST_UNCHECKED);
	Kbytes->SetCheck(BST_CHECKED);
	Mbytes->SetCheck(BST_UNCHECKED);
	LVCOLUMN col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT;
	listfiles->GetColumn(2, &col);
	col.pszText = L"Size (KB)";
	listfiles->SetColumn(2, &col);
	UpdateFileSizesDisplay();
	
}


void CsteamcloudDlg::OnBnClickedMbytes()
{
	sizeunit = 2;
	RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "SizeUnit", sizeunit);
	Bytes->SetCheck(BST_UNCHECKED);
	Kbytes->SetCheck(BST_UNCHECKED);
	Mbytes->SetCheck(BST_CHECKED);
	LVCOLUMN col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT;
	listfiles->GetColumn(2, &col);
	col.pszText = L"Size (MB)";
	listfiles->SetColumn(2, &col);
	UpdateFileSizesDisplay();
}

//void CsteamcloudDlg::OnBnClickedTestsave()
//{
//	CString cstr;
//	inputappid->GetWindowTextW(cstr);
//	for (size_t i = 0; i < cstr.GetLength(); ++i)
//	{
//		if (!isdigit(cstr[i]))
//		{
			//AfxMessageBox(_T("Zadejte pouze čísla."));
//			MessageBox(L"Please enter only numbers!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
//			return;
//		}
//	}
//	UpdateAppIdHistoryFromInput();
//	SaveComboBoxHistory();
//}

//void CsteamcloudDlg::OnBnClickedTestread()
//{
//	CString cstr;
//	inputappid->GetWindowTextW(cstr);
//	CString msg;
//	msg.Format(L"Entered number: %s", (LPCTSTR)cstr);
//	MessageBox(msg, L"Success", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
//}

void CsteamcloudDlg::LoadComboBoxHistory()
{
	const char* keyPath = "Software\\steamcloud";
	char appList[64] = { 0 };
	m_appidList.clear();
	m_appidList_orig.clear();
	m_appidMap.clear();

	int ret = RegGetKey(HKEY_CURRENT_USER, (LPSTR)keyPath, REG_SZ, KEY_QUERY_VALUE, "AppList", appList);
	if (ret != 1)
		return;

	size_t len = strlen(appList);

	for (size_t i = 0; i < len; ++i)
	{
		char regName[2] = { appList[i], 0 };
		char value[256] = { 0 };

		if (RegGetKey(HKEY_CURRENT_USER, (LPSTR)keyPath, REG_SZ, KEY_QUERY_VALUE, regName, value) == 1)
		{
			CString str = CA2W(value);
			m_appidMap[appList[i]] = str;
		}
	}

	// Vytvořit m_appidList podle pořadí z AppList, kde první je nejnovější
	m_appidList.clear();
	for (size_t i = 0; i < len; ++i)
	{
		m_appidList.push_back(m_appidMap[appList[i]]);
	}

	// Zrcadlíme originál
	m_appidList_orig = m_appidList;
	m_appList_keyOrder = appList;

	// Naplníme combobox (nejnovější nahoře)
	inputappid->ResetContent();
	for (const CString& val : m_appidList)
		inputappid->AddString(val);

	return;
}
void CsteamcloudDlg::UpdateAppIdHistoryFromInput()
{
	CString cstr;
	inputappid->GetWindowTextW(cstr);
	if (cstr.IsEmpty())
		return;

	// Odeber starý výskyt hodnoty, pokud existuje
	auto it = std::find(m_appidList.begin(), m_appidList.end(), cstr);
	if (it != m_appidList.end())
		m_appidList.erase(it);

	// Přidej hodnotu na začátek (nejnovější)
	m_appidList.insert(m_appidList.begin(), cstr);

	// Pokud máme více než 10, zapamatuj si nejstarší
	CString removedValue;
	if (m_appidList.size() > 10)
	{
		removedValue = m_appidList.back();
		m_appidList.pop_back();
	}

	// Pokud keyOrder není plný, inicializuj
	if (m_appList_keyOrder.size() < 10)
		m_appList_keyOrder = "abcdefghij";

	std::map<char, CString> newMap;
	std::set<char> usedKeys;

	// Zachováme staré mapování kromě odstraněné hodnoty
	for (const auto& [key, val] : m_appidMap)
	{
		if (val != removedValue)
			newMap[key] = val;
	}

	// Najdeme dostupné znaky podle původního pořadí
	std::vector<char> availableKeys;
	for (char ch : m_appList_keyOrder)
	{
		if (newMap.find(ch) == newMap.end())
			availableKeys.push_back(ch);
	}

	// Přiřadíme nové hodnoty do mapy
	for (const CString& val : m_appidList)
	{
		bool exists = false;
		for (const auto& [key, strVal] : newMap)
		{
			if (strVal == val)
			{
				usedKeys.insert(key);
				exists = true;
				break;
			}
		}
		if (!exists && !availableKeys.empty())
		{
			char newKey = availableKeys.front();
			availableKeys.erase(availableKeys.begin());
			newMap[newKey] = val;
			usedKeys.insert(newKey);
		}
	}

	m_appidMap = std::move(newMap);

	// Vytvoříme nový m_appList_keyOrder: pro každý prvek v m_appidList najdi jeho klíč a přidej (nejnovější první)
	std::string newOrder;
	for (const auto& val : m_appidList)
	{
		for (const auto& [key, mappedVal] : m_appidMap)
		{
			if (mappedVal == val)
			{
				newOrder += key;
				break;
			}
		}
	}

	m_appList_keyOrder = newOrder;

	// Obnovíme combobox (nejnovější nahoře)
	inputappid->ResetContent();
	for (const auto& val : m_appidList)
		inputappid->AddString(val);
	inputappid->SetCurSel(0);
}



void CsteamcloudDlg::SaveComboBoxHistory()
{
	const char* keyPath = "Software\\steamcloud";
	const char* keyNames = "abcdefghij";

	// Pokud se seznam nezměnil, není potřeba nic dělat
	if (m_appidList == m_appidList_orig)
		return;

	const size_t count = min(m_appidList.size(), 10);
	std::string appList;

	// Vytvoření AppList od nejnovější (první v m_appidList) po nejstarší (poslední)
	// Ale zapisujeme pozpátku, abychom měli AppList ve formátu: jihgfedcba
	for (auto it = m_appidList.begin(); it != m_appidList.end(); ++it)
	{
		const CString& val = *it;
		char foundChar = 0;

		// Najít, jestli už je hodnota v mapě
		for (auto& [keyChar, strVal] : m_appidMap)
		{
			if (strVal == val)
			{
				foundChar = keyChar;
				break;
			}
		}

		// Pokud není, přiřadíme nové písmeno (nejstarší z původního pořadí)
		if (foundChar == 0)
		{
			if (!m_appList_keyOrder.empty())
			{
				foundChar = m_appList_keyOrder.back();
				m_appList_keyOrder.pop_back();
			}
			else
			{
				// fallback pokud by klíčový řetězec byl prázdný
				foundChar = keyNames[appList.size()];
			}

			m_appidMap[foundChar] = val;
		}

		appList.push_back(foundChar);
	}

	// Uložit hodnoty podle mapy
	for (const auto& [keyChar, val] : m_appidMap)
	{
		char regName[2] = { keyChar, 0 };
		std::string strVal = CT2A(val);
		RegSetKey(HKEY_CURRENT_USER, (LPSTR)keyPath, REG_SZ, KEY_SET_VALUE, regName, strVal);
	}

	// Uložit AppList (např. jihgfedcba)
	RegSetKey(HKEY_CURRENT_USER, (LPSTR)keyPath, REG_SZ, KEY_SET_VALUE, (LPSTR)"AppList", appList);

	// Aktualizace originálu
	m_appidList_orig = m_appidList;
	m_appList_keyOrder = appList;
}

void CsteamcloudDlg::UpdateFileSizesDisplay()
{
	int rowCount = listfiles->GetItemCount();

	for (int i = 0; i < rowCount; ++i)
	{
		CString filename = listfiles->GetItemText(i, 0); // 1. sloupec: název souboru

		auto it = m_fileSizesBytes.find(filename);
		if (it != m_fileSizesBytes.end())
		{
			int64_t sizeBytes = it->second;
			wchar_t buf[100] = {};

			switch (sizeunit)
			{
			case 0: // Byty
				swprintf_s(buf, L"%lld", sizeBytes);
				break;
			case 1: // KB
				swprintf_s(buf, L"%.4f", (float)sizeBytes / 1024);
				break;
			case 2: // MB
				swprintf_s(buf, L"%.4f", (float)sizeBytes / (1024 * 1024));
				break;
			default:
				swprintf_s(buf, L"%lld", sizeBytes);
				break;
			}

			listfiles->SetItemText(i, 2, buf); // 3. sloupec
		}
		else
		{
			// Pokud název není v mapě, můžeš případně vymazat velikost nebo nechat jak je
			listfiles->SetItemText(i, 2, L"");
		}
	}

	// Aktualizovat kvótu stejným způsobem, pokud chceš
	CString quotaText;
	switch (sizeunit)
	{
	case 0:
		quotaText.Format(L"%llu/%llu Bytes used", m_quotaUsed, m_quotaTotal);
		break;
	case 1:
		quotaText.Format(L"%.4f/%.4f KB used", (float)m_quotaUsed / 1024, (float)m_quotaTotal / 1024);
		break;
	case 2:
		quotaText.Format(L"%.4f/%.4f MB used", (float)m_quotaUsed / (1024 * 1024), (float)m_quotaTotal / (1024 * 1024));
		break;
	default:
		quotaText.Format(L"%llu/%llu Bytes used", m_quotaUsed, m_quotaTotal);
		break;
	}
	SetDlgItemTextW(IDC_QUOTA, quotaText);
}
