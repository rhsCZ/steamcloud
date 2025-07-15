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
void CsteamcloudDlg::OnLvnColumnClickListFiles(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	int clickedColumn = pNMLV->iSubItem;

	// Toggle order if same column clicked
	if (clickedColumn == m_nSortedColumn)
		m_bSortAscending = !m_bSortAscending;
	else {
		m_nSortedColumn = clickedColumn;
		m_bSortAscending = true; // default to ascending
	}
	SortInfo sortInfo = { clickedColumn, m_bSortAscending };
	listfiles->SortItems(CompareFunc, (LPARAM)&sortInfo);
	*pResult = 0;
}

int CALLBACK CsteamcloudDlg::CompareFunc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	SortInfo* info = reinterpret_cast<SortInfo*>(lParamSort);
	FileRow* row1 = reinterpret_cast<FileRow*>(lParam1);
	FileRow* row2 = reinterpret_cast<FileRow*>(lParam2);

	int result = 0;
	switch (info->column) {
	case 0: // Name
		result = row1->name.CompareNoCase(row2->name);
		break;
	case 1: // Timestamp
		result = static_cast<int>(row2->timestamp - row1->timestamp); // nejnovější > nejstarší
		break;
	case 2: // Size
		result = row2->size - row1->size; // větší > menší
		break;
	default:
		break;
	}

	return info->ascending ? result : -result;
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
		if (PeekNamedPipe(m_hResponsePipe, NULL, 0, NULL, &bytesAvailable, NULL) && bytesAvailable > 0)
		{
			DWORD bytesRead = 0;
			if (ReadFile(m_hResponsePipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
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
	if (!m_hResponsePipe || m_hResponsePipe == INVALID_HANDLE_VALUE) {
		MessageBox(L"Pipe's not open.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	bool empty = false;
	Clearlist();
	m_fileSizesBytes.clear();
	

	// 1. send "list" command
	const char* cmdList = "list\n";
	DWORD written = 0;
	while(pipeblocked)
	{
		Sleep(100); // Wait until the pipe is not blocked
	}
	pipeblocked = true; // Set the flag to indicate the pipe is blocked
	if (!WriteFile(m_hRequestPipe, cmdList, (DWORD)strlen(cmdList), &written, NULL)) {
		pipeblocked = false; // Reset the flag if write fails
		MessageBox(L"Unable to write 'list to pipe'", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	pipeblocked = false; // Reset the flag after writing
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

	if (j.is_array() && j.empty()) {
		empty = true;
	}
	if(!empty)
	{ 
		int index = 0;
		for (auto& [key, val] : j.items()) {
			if (!val.is_object()) continue;

			std::string name = val.value("name", "");
			uint64_t timestamp = val.value("timestamp", 0);
			int size = val.value("size", 0);
			bool exists = val.value("exists", false);
			bool persisted = val.value("persistent", false);
			FileRow row;
			row.name = CA2W(name.c_str());
			row.timestamp = (__time64_t)timestamp;
			row.size = size;
			row.exists = exists;
			row.persisted = persisted;
			m_fileRowData.push_back(row);
			CString namecs(name.c_str());
			m_fileSizesBytes[namecs] = size;
			CString nameW(name.c_str());
			CString sizeW, dateW;

			// Size formatting based on sizeunit
			switch (sizeunit) {
			case 0: sizeW.Format(L"%d", size); break;
			case 1: sizeW.Format(L"%.4f", (float)size / 1024); break;
			case 2: sizeW.Format(L"%.4f", (float)size / (1024 * 1024)); break;
			default: sizeW.Format(L"%d", size); break;
			}

			// Date formatting
			wchar_t dateBuf[100] = {};
			tm* timeinfo = localtime((time_t*)&timestamp);
			wcsftime(dateBuf, sizeof(dateBuf) / sizeof(wchar_t), L"%e.%m.%Y %H:%M:%S", timeinfo);
			dateW = dateBuf;

			int Itemindex = listfiles->InsertItem(index, nameW);
			listfiles->SetItemText(Itemindex, 1, dateW);
			listfiles->SetItemText(Itemindex, 2, sizeW);
			listfiles->SetItemText(Itemindex, 3, persisted ? L"true" : L"false");
			listfiles->SetItemText(Itemindex, 4, exists ? L"true" : L"false");
			listfiles->SetItemData(Itemindex, (DWORD_PTR)&m_fileRowData.back());
			index++;
		}
		if (m_nSortedColumn >= 0)
		{
			SortInfo info = { m_nSortedColumn, m_bSortAscending };
			listfiles->SortItems(CompareFunc, (LPARAM)&info);
		}
	}
	// 3. quota
	const char* cmdQuota = "quota\n";
	while(pipeblocked)
	{
		Sleep(100); // Wait until the pipe is not blocked
	}
	pipeblocked = true; // Set the flag to indicate the pipe is blocked
	if (!WriteFile(m_hRequestPipe, cmdQuota, (DWORD)strlen(cmdQuota), &written, NULL)) {
		pipeblocked = false; // Reset the flag if write fails
		MessageBox(L"Cannot write the 'quota' command to pipe.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	pipeblocked = false; // Reset the flag after writing
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
	ON_NOTIFY(LVN_COLUMNCLICK, IDC_LISTFILES, &CsteamcloudDlg::OnLvnColumnClickListFiles)
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
	m_hRequestPipe = CreateNamedPipeW(
		L"\\\\.\\pipe\\SteamDlgRequestPipe",
		PIPE_ACCESS_OUTBOUND,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		4096,
		4096,
		0,
		NULL);

	if (m_hRequestPipe == INVALID_HANDLE_VALUE)
	{
		MessageBox(L"Error creating pipe.", L"Error", MB_OK | MB_ICONERROR);
	}
	m_hResponsePipe = CreateNamedPipeW(
		L"\\\\.\\pipe\\SteamDlgResponsePipe",
		PIPE_ACCESS_INBOUND,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		4096,
		4096,
		0,
		NULL);

	if (m_hResponsePipe == INVALID_HANDLE_VALUE)
	{
		MessageBox(L"Error creating response pipe.", L"Error", MB_OK | MB_ICONERROR);
	}


	if(m_hRequestPipe != INVALID_HANDLE_VALUE && m_hResponsePipe != INVALID_HANDLE_VALUE)
	{
		DisconnectNamedPipe(m_hRequestPipe);
		DisconnectNamedPipe(m_hResponsePipe);
		m_RequestThreadEnabled = true;
		m_ResponseThreadEnabled = true;
		m_RequestpipeThread = std::thread(&CsteamcloudDlg::RequestPipeThread, this);
		m_ResponsepipeThread = std::thread(&CsteamcloudDlg::ResponsePipeThread, this);
		if(m_RequestpipeThread.joinable())
		{
			m_RequestpipeThread.detach();
		}
		if(m_ResponsepipeThread.joinable())
		{
			m_ResponsepipeThread.detach();
		}
	} else
	{
		exit(1);
	}
	//AfxInitRichEdit2();
	while (!m_RequestThreadRunning)
	{
		Sleep(10); // Wait for the request thread to start
	}
	while (!m_ResponseThreadRunning)
	{
		Sleep(10); // Wait for the response thread to start
	}
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
		listfiles->SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
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
		if (m_hWorkerProcess && m_hRequestPipe)
		{
			const char* exitCmd = "exit\n";
			DWORD bytesWritten = 0;
			while (pipeblocked)
			{
				Sleep(100); // Wait until the pipe is not blocked
			}
			pipeblocked = true; // Set the flag to indicate the pipe is blocked
			if (WriteFile(m_hRequestPipe, exitCmd, (DWORD)strlen(exitCmd), &bytesWritten, NULL))
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
		pipeblocked = false; // Reset the flag
	}
	m_RequestThreadEnabled = false;
	m_ResponseThreadEnabled = false;
	if(m_RequestThreadWaiting)
	{
		// Create a dummy client to unblock the request thread if it's waiting for a connection
		HANDLE dummyClient = CreateFileW(
			L"\\\\.\\pipe\\SteamDlgRequestPipe",
			GENERIC_READ,
			0,
			NULL,
			OPEN_EXISTING,
			0,
			NULL);
		DWORD err = GetLastError();
		if (dummyClient != INVALID_HANDLE_VALUE)
		{
			CloseHandle(dummyClient);
		}
	}
	if(m_ResponseThreadWaiting)
	{
		// Create a dummy client to unblock the response thread if it's waiting for a connection
		HANDLE dummyClient = CreateFileW(
			L"\\\\.\\pipe\\SteamDlgResponsePipe",
			GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			0,
			NULL);
		DWORD err = GetLastError();
		if (dummyClient != INVALID_HANDLE_VALUE)
		{
			CloseHandle(dummyClient);
		}
	}
	while(m_RequestThreadRunning)
	{
		Sleep(100); // Wait for threads to finish
	}
	while(m_ResponseThreadRunning)
	{
		Sleep(100); // Wait for threads to finish
	}
	if (m_hRequestPipe && m_hRequestPipe != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hRequestPipe);
		m_hRequestPipe = NULL;
	}
	if (m_hResponsePipe && m_hResponsePipe != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hResponsePipe);
		m_hResponsePipe = NULL;
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
	if ((m_hWorkerProcess == NULL || m_hWorkerProcess == INVALID_HANDLE_VALUE) && (!m_ResponseThreadWaiting || !m_RequestThreadWaiting))
	{
		Sleep(300); // Ensure the pipes are ready.
		if (!m_ResponseThreadWaiting || !m_RequestThreadWaiting)
		{
			MessageBox(L"Pipe threads are not ready, please try again later.", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
			return;
		}
	}
	Clearlist();
	// Obtain the AppID from the input field
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
		if(GetProcessPIDByName(L"steam-worker.exe") != 0) {
			KillAllSteamWorkerProcesses(); // Ensure no other worker processes are running
			if(GetProcessPIDByName(L"steam-worker.exe") != 0) {
				MessageBox(L"Unable to kill steam-worker.exe process. Please try terminate it or restart PC.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
				return;
			}
			return;
		}
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
			DeleteFileW(workerPath); // Remove Previous Version if exists - needed when steam-worker.exe is updated
		}
		if (!ExtractResourceToFile(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_WORKER), RT_RCDATA, workerPath)) {
			MessageBox(L"Unable to extract steam-worker.exe!", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
			return;
		}
		if (!PathFileExistsW(dllPath)) {
			DeleteFileW(dllPath); // Remove Previous Version if exists - needed when dll is updated
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
		Sleep(200); // Give the worker some time to initialize
		// Wait for the worker to send "READY" message - this is crucial to ensure the worker is ready before sending any commands
		std::string output;
		const DWORD timeoutMs = 10000;

		if (!ReadFromPipeWithTimeout(timeoutMs, output))
		{
			MessageBox(L"Worker doesn't respond within 10s.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
			TerminateProcess(m_hWorkerProcess, 1);
			CloseHandle(m_hWorkerProcess);
			m_hWorkerProcess = NULL;
			return;
		}

		if (output != "READY")
		{
			MessageBox(L"Worker didn't send READY", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
			TerminateProcess(m_hWorkerProcess, 1);
			CloseHandle(m_hWorkerProcess);
			m_hWorkerProcess = NULL;
			return;
		}
	}
	Sleep(200); // Ensure the worker is ready to receive commands
	// Send the connect command to the worker with the specified AppID
	std::string cmd = "connect " + std::to_string(appid) + "\n";
	DWORD bytesWritten = 0;
	while (pipeblocked)
	{
		Sleep(100); // Wait until the pipe is not blocked
	}
	pipeblocked = true; // Set the flag to indicate the pipe is blocked
	if (!WriteFile(m_hRequestPipe, cmd.c_str(), (DWORD)cmd.length(), &bytesWritten, NULL)) {
		pipeblocked = false; // Reset the flag
		MessageBox(L"Can't write to pipe.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	pipeblocked = false; // Reset the flag
	// Wait for a response from the worker within 5seconds
	string response;
	if (!ReadFromPipeWithTimeout(1000, response))
	{
		// If we reach here and gotReply is still false, it means we timed out waiting for a reply
		MessageBox(L"No response from worker", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	try {
		auto parsed = json::parse(response);
		if (parsed.is_array() && parsed.size() > 0) {
			std::string status = parsed[0].value("status", "");

			if (status == "connected") {
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
				return;
			}
			else if (status == "SteamAPI_Init failed" || status == "SteamAPI not initialized") {
				Clearlist();
				download->EnableWindow(0);
				deletefile->EnableWindow(0);
				upload->EnableWindow(0);
				uploaddir->EnableWindow(0);
				refresh->EnableWindow(0);
				quota->ShowWindow(0);
				disconnect->EnableWindow(0);
				init = false;
				CString msg(status.c_str());
				MessageBox(msg, L"SteamAPI Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
				return;
			}
		}
		// If we reach here, it means the response is not in the expected format
		Clearlist();
		download->EnableWindow(0);
		deletefile->EnableWindow(0);
		upload->EnableWindow(0);
		uploaddir->EnableWindow(0);
		refresh->EnableWindow(0);
		quota->ShowWindow(0);
		disconnect->EnableWindow(0);
		init = false;
		CString jsonStr(response.c_str());
		MessageBox(jsonStr, L"Worker Response", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	catch (const std::exception& e) {
		Clearlist();
		download->EnableWindow(0);
		deletefile->EnableWindow(0);
		upload->EnableWindow(0);
		uploaddir->EnableWindow(0);
		refresh->EnableWindow(0);
		quota->ShowWindow(0);
		disconnect->EnableWindow(0);
		init = false;
		CString msg;
		msg.Format(L"Error when parsing JSON: %S", e.what());
		MessageBox(msg, L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
}


void CsteamcloudDlg::OnBnClickedDelete()
{
	if(!init)
	{
		// this should not happen, but just in case
		MessageBox(L"Please connect to Steam first!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
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
	while (pipeblocked)
	{
		Sleep(100); // Wait until the pipe is not blocked
	}
	pipeblocked = true; // Set the flag to indicate the pipe is blocked
	if (!WriteFile(m_hRequestPipe, command.c_str(), (DWORD)command.size(), &written, NULL))
	{
		pipeblocked = false; // Reset the flag
		MessageBox(L"Failed to send delete command to worker!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	pipeblocked = false; // Reset the flag
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

		if (j.is_array() && j.empty())
			return; // Do not show a message box if no files were deleted

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

	MessageBox(result, L"Delete result", MB_OK | MB_TOPMOST);
	Clearlist();
	GetFiles();
}


void CsteamcloudDlg::OnBnClickedUpload()
{
	if (!init)
	{
		// this should not happen, but just in case
		MessageBox(L"Please connect to Steam first!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	bool fileopened[MAX_FILES_COUNT] = { false };
	bool filesizeok[MAX_FILES_COUNT] = { false };
	wchar_t filelist[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t filename[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t dirname[MAX_PATH] = { 0 };
	int filecount = 0;
	CString filenames;
	DWORD maxfiles = 10;
	fstream file;

	// Create a file dialog to select files for upload
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
	INT_PTR ressss = dlg.DoModal();
	if (ressss != IDOK)
	{ 
		return;
	}
	// Get the selected files and directory
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

	// Create the upload command
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

	// Send the upload command to the worker
	std::string commandStr = uploadCommand.str();
	DWORD bytesWritten = 0;
	while (pipeblocked)
	{
		Sleep(100); // Wait until the pipe is not blocked
	}
	pipeblocked = true; // Set the flag to indicate the pipe is blocked
	if (!WriteFile(m_hRequestPipe, commandStr.c_str(), (DWORD)commandStr.size(), &bytesWritten, NULL))
	{
		pipeblocked = false; // Reset the flag
		MessageBox(L"Unable to write to pipe!", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	pipeblocked = false; // Reset the flag
	// Wait for a response from the worker
	std::string output;
	if (!ReadFromPipeWithTimeout(10000, output))
	{
		MessageBox(L"Unable to read response from pipe.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	// Parse the JSON response
	CString msg;
	try {
		auto j = json::parse(output);
		for (const auto& entry : j)
		{
			if (entry.contains("error"))
			{
				CString namePart;
				if (entry.contains("name")) {
					namePart = CString(entry["name"].get<std::string>().c_str()) + L": ";
				}
				else if (entry.contains("input")) {
					namePart = CString(entry["input"].get<std::string>().c_str()) + L": ";
				}
				msg += namePart + CString(entry["error"].get<std::string>().c_str()) + L"\r\n";
			}
			else if (entry.contains("name") && entry.contains("size"))
			{
				msg += CString(entry["name"].get<std::string>().c_str()) + L": uploaded (" +
					std::to_wstring(entry["size"].get<int>()).c_str() + L" bytes)\r\n";
			}
			else
			{
				msg += L"Unknown entry format in response.\r\n";
			}
		}
	}
	catch (...) {
		MessageBox(L"Answer from worker is not valid JSON!", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}

	if (!msg.IsEmpty()) {
		MessageBox(msg, L"Upload results", MB_OK | MB_TOPMOST);
	}


	Clearlist();
	GetFiles();
}



void CsteamcloudDlg::OnBnClickedDirupload()
{
	if (!init)
	{
		// this should not happen, but just in case
		MessageBox(L"Please connect to Steam first!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
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

			// Convert backslashes to forward slashes
			for (int i = 0; dirupload[i]; ++i)
			{
				if (dirupload[i] == L'\\')
					dirupload[i] = L'/';
			}

			// Delete trailing slashes
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

			// Making the upload command
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
			while (pipeblocked)
			{
				Sleep(100); // Wait until the pipe is not blocked
			}
			pipeblocked = true; // Set the flag to indicate the pipe is blocked
			if (!WriteFile(m_hRequestPipe, commandStr.c_str(), (DWORD)commandStr.size(), &written, NULL))
			{
				pipeblocked = false; // Reset the flag
				MessageBox(L"Unable to write to pipe!", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
				return;
			}
			pipeblocked = false; // Reset the flag
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
					std::wstring name;
					if (entry.contains("name"))
						name = CA2W(entry["name"].get<std::string>().c_str());
					else if (entry.contains("input"))
						name = CA2W(entry["input"].get<std::string>().c_str());
					else
						name = L"<unknown>";

					output += name;

					if (entry.contains("error"))
					{
						output += L" - ";
						output += CA2W(entry["error"].get<std::string>().c_str());
					}
					else if (entry.contains("status") && entry["status"] == "uploaded")
					{
						output += L": uploaded (" + std::to_wstring(entry["size"].get<int>()) + L" bytes)";
					}

					output += L"\r\n";
				}

				if (!output.empty())
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
	if (!init)
	{
		// this should not happen, but just in case
		MessageBox(L"Please connect to Steam first!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	constexpr int MAX_SELECTED = 10;
	int selectedIndices[MAX_SELECTED];
	int count = 0;

	// Get selected items from the list control
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
	while (pipeblocked)
	{
		Sleep(100); // Wait until the pipe is not blocked
	}
	pipeblocked = true; // Set the flag to indicate the pipe is blocked
	if (!WriteFile(m_hRequestPipe, cmd.c_str(), (DWORD)cmd.length(), &bytesWritten, NULL))
	{
		pipeblocked = false; // Reset the flag
		MessageBox(L"Unable to write to pipe", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	pipeblocked = false; // Reset the flag
	// Reading response from worker
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

		if (result.is_array() && result.empty())
			return; // Don't display anything if the array is empty

		CString statusMsg;
		for (const auto& item : result)
		{
			CString name = CA2W(item.value("name", "").c_str());
			CString status = CA2W(item.value("status", "").c_str());
			int size = item.value("size", 0);

			if (status.IsEmpty())
			{
				// If status is empty, check for error
				if (item.contains("error"))
				{
					CString error = CA2W(item["error"].get<std::string>().c_str());
					statusMsg += name + L": " + error + L"\r\n";
				}
				else
				{
					statusMsg += name + L": unknown status\r\n";
				}
			}
			else
			{
				statusMsg += name + L": " + status + L" (" + std::to_wstring(size).c_str() + L" bytes)\r\n";
			}
		}

		MessageBox(statusMsg, L"Download status", MB_OK | MB_TOPMOST);
	}
	catch (...)
	{
		MessageBox(L"Error on parsing JSON response.", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
	}
}

DWORD CsteamcloudDlg::GetProcessPIDByName(const wchar_t* name) //Search Process ID by Name
{
	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (Process32First(snapshot, &entry) == TRUE)
	{
		while (Process32Next(snapshot, &entry) == TRUE)
		{

			if (_wcsicmp(entry.szExeFile, name) == 0)
			{
				CloseHandle(snapshot);
				return entry.th32ProcessID;

			}
		}
		CloseHandle(snapshot);
		return NULL;
	}
	else
	{
		CloseHandle(snapshot);
		return NULL;
	}
}

void CsteamcloudDlg::KillAllSteamWorkerProcesses()
{
	const wchar_t* processName = L"steam-worker.exe";

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return;

	PROCESSENTRY32 entry;
	entry.dwSize = sizeof(PROCESSENTRY32);

	if (Process32First(snapshot, &entry)) {
		do {
			if (_wcsicmp(entry.szExeFile, processName) == 0) {
				HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
				if (hProcess != NULL) {
					TerminateProcess(hProcess, 1);
					CloseHandle(hProcess);
				}
			}
		} while (Process32Next(snapshot, &entry));
	}

	CloseHandle(snapshot);
}

void CsteamcloudDlg::OnBnClickedRefresh()
{
	if (!init)
	{
		// this should not happen, but just in case
		MessageBox(L"Please connect to Steam first!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	Clearlist();
	GetFiles();
}

void CsteamcloudDlg::OnBnClickedDisconnect()
{
	if (!init) return; // If not initialized, do nothing
	// Attempt to send exit command to worker process
	if (m_hWorkerProcess && m_hRequestPipe)
	{
		const char* exitCmd = "exit\n";
		DWORD bytesWritten = 0;
		while (pipeblocked)
		{
			Sleep(100); // Wait until the pipe is not blocked
		}
		pipeblocked = true; // Set the pipe to blocked state to prevent further writes until this command is processed
		if (!WriteFile(m_hRequestPipe, exitCmd, (DWORD)strlen(exitCmd), &bytesWritten, NULL)) {
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
					// This should never happen, but if it does, we inform the user
					MessageBox(L"The worker process cannot be terminated manually.", L"Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
				}
				else {
					//MessageBox(L"Worker was terminated manually. Disconnected", L"Info", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
				}
			}
			else {
				//MessageBox(L"Worker has ended successfully. Disconnected.", L"Info", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
			}
		}
	}
	pipeblocked = false; // Reset the pipe blocked state after sending the exit command
	MessageBox(L"Disconnected from Steam Cloud.", L"Info", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
	init = false; // Reset the initialization flag
	// Close worker process handle if it exists
	if (m_hWorkerProcess)
	{
		CloseHandle(m_hWorkerProcess);
		m_hWorkerProcess = NULL;
	}

	// Reset UI elements
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

	m_appidList.clear();
	for (size_t i = 0; i < len; ++i)
	{
		m_appidList.push_back(m_appidMap[appList[i]]);
	}

	m_appidList_orig = m_appidList;
	m_appList_keyOrder = appList;

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

	auto it = std::find(m_appidList.begin(), m_appidList.end(), cstr);
	if (it != m_appidList.end())
		m_appidList.erase(it);

	m_appidList.insert(m_appidList.begin(), cstr);

	CString removedValue;
	if (m_appidList.size() > 10)
	{
		removedValue = m_appidList.back();
		m_appidList.pop_back();
	}

	if (m_appList_keyOrder.size() < 10)
		m_appList_keyOrder = "abcdefghij";

	std::map<char, CString> newMap;
	std::set<char> usedKeys;

	for (const auto& [key, val] : m_appidMap)
	{
		if (val != removedValue)
			newMap[key] = val;
	}

	std::vector<char> availableKeys;
	for (char ch : m_appList_keyOrder)
	{
		if (newMap.find(ch) == newMap.end())
			availableKeys.push_back(ch);
	}

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

	inputappid->ResetContent();
	for (const auto& val : m_appidList)
		inputappid->AddString(val);
	inputappid->SetCurSel(0);
}



void CsteamcloudDlg::SaveComboBoxHistory()
{
	const char* keyPath = "Software\\steamcloud";
	const char* keyNames = "abcdefghij";

	if (m_appidList == m_appidList_orig)
		return;

	const size_t count = min(m_appidList.size(), 10);
	std::string appList;

	for (auto it = m_appidList.begin(); it != m_appidList.end(); ++it)
	{
		const CString& val = *it;
		char foundChar = 0;

		for (auto& [keyChar, strVal] : m_appidMap)
		{
			if (strVal == val)
			{
				foundChar = keyChar;
				break;
			}
		}

		if (foundChar == 0)
		{
			if (!m_appList_keyOrder.empty())
			{
				foundChar = m_appList_keyOrder.back();
				m_appList_keyOrder.pop_back();
			}
			else
			{
				foundChar = keyNames[appList.size()];
			}

			m_appidMap[foundChar] = val;
		}

		appList.push_back(foundChar);
	}

	for (const auto& [keyChar, val] : m_appidMap)
	{
		char regName[2] = { keyChar, 0 };
		std::string strVal = CT2A(val);
		RegSetKey(HKEY_CURRENT_USER, (LPSTR)keyPath, REG_SZ, KEY_SET_VALUE, regName, strVal);
	}

	RegSetKey(HKEY_CURRENT_USER, (LPSTR)keyPath, REG_SZ, KEY_SET_VALUE, (LPSTR)"AppList", appList);

	m_appidList_orig = m_appidList;
	m_appList_keyOrder = appList;
}

void CsteamcloudDlg::UpdateFileSizesDisplay()
{
	int rowCount = listfiles->GetItemCount();

	for (int i = 0; i < rowCount; ++i)
	{
		CString filename = listfiles->GetItemText(i, 0);

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

			listfiles->SetItemText(i, 2, buf);
		}
		else
		{
			listfiles->SetItemText(i, 2, L"");
		}
	}

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
void CsteamcloudDlg::RequestPipeThread()
{
	while (m_RequestThreadEnabled)
	{
		m_RequestThreadRunning = true;
		m_RequestThreadWaiting = true; // Set the request thread waiting flag to true indicating the thread is waiting for a connection
		BOOL connected = ConnectNamedPipe(m_hRequestPipe, NULL) ?
			TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
		m_RequestThreadWaiting = false; // Set the request thread waiting flag to false indicating the thread is no longer waiting for a connection
		if (!m_RequestThreadEnabled) break; // If the request thread is disabled, exit the loop immediately
		if (!connected) {
			Sleep(100); // delay before retrying connection
			continue;
		}
		
		m_brokenPipe = false; // Reset broken pipe status
		while (m_RequestThreadEnabled)
		{
			m_statusrequestpipe = true;
			const char* ping = ".\n";
			DWORD written = 0;
			BOOL ok = 1;
			if (!pipeblocked) // When pipe is not blocked, send ping to worker, otherwise skip it, so we don't block the pipe and don't interrupt user actions
			{
				pipeblocked = true; // Set the pipe blocked flag to true to prevent further actions while sending ping
				ok = WriteFile(m_hRequestPipe, ping, (DWORD)strlen(ping), &written, NULL);
				pipeblocked = false; // Reset the pipe blocked flag after sending ping
			}
				 
			if (!ok) {
				DWORD err = GetLastError();
				if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED || err == ERROR_NO_DATA) {
					m_brokenPipe = true;
					m_statusrequestpipe = false;
					break;
				}
			}

			Sleep(800); // sleep for 150 ms to avoid busy waiting and reduce CPU usage
		}

		// pipe is broken, we need to disconnect it and wait for the next connection
		DisconnectNamedPipe(m_hRequestPipe);
	}
	m_RequestThreadRunning = false; // Set the request thread running flag to false indicating the thread has finished execution
}
void CsteamcloudDlg::ResponsePipeThread()
{
	while (m_ResponseThreadEnabled)
	{
		m_ResponseThreadRunning = true;
		// wait for client to connect to response pipe
		m_ResponseThreadWaiting = true;
		BOOL connected = ConnectNamedPipe(m_hResponsePipe, NULL) ?
			TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
		m_ResponseThreadWaiting = false;
		if (!m_ResponseThreadEnabled) break; // If the response thread is disabled, exit the loop immediately
		if (!connected)
		{
			Sleep(100); // delay before retrying connection
			continue;
		}
		m_statusresponsepipe = true;
		// wait until another thread sets m_brokenPipe to true
		while (!m_brokenPipe && m_ResponseThreadEnabled)
		{
			Sleep(800); // sleep for 150 ms to avoid busy waiting and reduce CPU usage
		}
		m_statusresponsepipe = false;
		// pipe is broken, we need to disconnect it and wait for the next connection
		DisconnectNamedPipe(m_hResponsePipe);
	}
	m_ResponseThreadRunning = false; // Set the response thread running flag to false indicating the thread has finished execution
}

