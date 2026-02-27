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
	clearing = true; // Set the flag to indicate clearing is in progress
	BOOL ret = listfiles->DeleteAllItems(); // Clear all items in the list control
	int count = listfiles->GetItemCount();
	for (int i = 0; i < count; i++)
	{
		
		int test = listfiles->DeleteItem(0);
	}
	clearing = false; // Reset the flag after clearing
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

bool CsteamcloudDlg::ReadFromPipeWithTimeout(ULONGLONG timeoutMs, std::string& output)
{
	const DWORD interval = 100; // 100ms polling
	ULONGLONG startTime = GetTickCount64();
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

bool CsteamcloudDlg::SendCommandAndReadResponse(const std::string& command, ULONGLONG timeoutMs, std::string& response)
{
	std::lock_guard<std::mutex> lock(m_pipeIoMutex);
	if (!m_hRequestPipe || m_hRequestPipe == INVALID_HANDLE_VALUE ||
		!m_hResponsePipe || m_hResponsePipe == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	DWORD bytesWritten = 0;
	if (!WriteFile(m_hRequestPipe, command.c_str(), static_cast<DWORD>(command.size()), &bytesWritten, NULL))
	{
		return false;
	}

	return ReadFromPipeWithTimeout(timeoutMs, response);
}

bool CsteamcloudDlg::EnsureWorkerIdle(std::wstring& statusMessage)
{
	std::string response;
	if (!SendCommandAndReadResponse("status\n", 4000, response))
	{
		statusMessage = L"Worker status is unavailable (pipe timeout or write failure).";
		return false;
	}

	try
	{
		auto parsed = json::parse(response);
		if (!parsed.is_array() || parsed.empty())
		{
			statusMessage = L"Worker returned invalid status response.";
			return false;
		}

		const auto& first = parsed[0];
		const std::string status = first.value("status", "");
		if (status == "idle")
		{
			return true;
		}

		if (status == "working" || status == "busy")
		{
			const std::string operation = first.value("operation", "unknown");
			CStringW msg;
			msg.Format(L"Worker is currently busy (%S). Please wait and try again.", operation.c_str());
			statusMessage = std::wstring(msg);
			return false;
		}

		CStringW msg;
		msg.Format(L"Worker status is '%S'. Please wait and try again.", status.c_str());
		statusMessage = std::wstring(msg);
		return false;
	}
	catch (...)
	{
		statusMessage = L"Worker returned malformed status JSON.";
		return false;
	}
}
void CsteamcloudDlg::GetFiles()
{
	if (!TryBeginAction()) return;
	while(clearing)
	{
		Sleep(100); // Wait until the clearing operation is finished
	}
	thread([this]() {
		if (!m_hResponsePipe || m_hResponsePipe == INVALID_HANDLE_VALUE) {
			PostAsyncMessage(L"Error", L"Pipe's not open.", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		std::wstring statusMessage;
		if (!EnsureWorkerIdle(statusMessage)) {
			PostAsyncMessage(L"Worker busy", statusMessage.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
			EndAction();
			return;
		}

		std::string listJsonStr;
		if (!SendCommandAndReadResponse("list\n", 10000, listJsonStr)) {
			PostAsyncMessage(L"Error", L"Unable to write 'list to pipe'", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		json listJson;
		try {
			listJson = json::parse(listJsonStr);
		}
		catch (...) {
			PostAsyncMessage(L"Error", L"Error on parsing JSON result from pipe(list)", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		std::vector<FileRow> rows;
		std::map<CString, int64_t> sizes;
		if (!listJson.is_array()) {
			PostAsyncMessage(L"Error", L"Invalid list response format.", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		for (auto& val : listJson) {
			if (!val.is_object()) continue;
			std::string name = val.value("name", "");
			FileRow row;
			row.name = CA2W(name.c_str());
			row.timestamp = static_cast<__time64_t>(val.value("timestamp", 0ULL));
			row.size = val.value("size", 0);
			row.exists = val.value("exists", false);
			row.persisted = val.value("persistent", false);
			rows.push_back(row);
			sizes[row.name] = row.size;
		}

		std::string quotaJsonStr;
		if (!SendCommandAndReadResponse("quota\n", 5000, quotaJsonStr)) {
			PostAsyncMessage(L"Error", L"Cannot write the 'quota' command to pipe.", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		json quotaJsonArray;
		try {
			quotaJsonArray = json::parse(quotaJsonStr);
		}
		catch (...) {
			PostAsyncMessage(L"Error", L"Error parsing JSON response (quota).", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		if (!quotaJsonArray.is_array() || quotaJsonArray.empty()) {
			PostAsyncMessage(L"Error", L"Invalid quota response format.", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		const json& quotaJson = quotaJsonArray[0];
		{
			std::lock_guard<std::mutex> lock(m_dataMutex);
			m_fileRowData = std::move(rows);
			m_fileSizesBytes = std::move(sizes);
			m_quotaUsed = quotaJson.value("used", 0ULL);
			m_quotaTotal = quotaJson.value("total", 0ULL);
			m_quotaAvailable = quotaJson.value("available", 0ULL);
		}

		PostMessage(WM_UPDATE_QUOTA, 0, 0);
		PostMessage(WM_UPDATE_LIST, 0, 0);
		EndAction();
	}).detach();
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
	ON_MESSAGE(WM_UPDATE_LIST, &CsteamcloudDlg::OnUpdateList)
	ON_MESSAGE(WM_UPDATE_QUOTA, &CsteamcloudDlg::OnUpdateQuota)
	ON_MESSAGE(WM_ENABLE_CONTROL, &CsteamcloudDlg::OnEnableControl)
	ON_MESSAGE(WM_DISABLE_CONTROL, &CsteamcloudDlg::OnDisableControl)
	ON_MESSAGE(WM_CLEAR_LIST, &CsteamcloudDlg::OnClearList)
	ON_MESSAGE(WM_UPDATE_COMBOBOX, &CsteamcloudDlg::OnUpdateComboBox)
	ON_MESSAGE(WM_SHOW_ASYNC_MESSAGE, &CsteamcloudDlg::OnShowAsyncMessage)
	ON_MESSAGE(WM_REQUEST_GETFILES, &CsteamcloudDlg::OnRequestGetFiles)
END_MESSAGE_MAP()

LRESULT CsteamcloudDlg::OnUpdateList(WPARAM wParam, LPARAM lParam)
{
	RefreshListFromData();
	return 0;
}
LRESULT CsteamcloudDlg::OnUpdateQuota(WPARAM wParam, LPARAM lParam)
{
	UpdateQuota();
	return 0;
}
LRESULT CsteamcloudDlg::OnEnableControl(WPARAM wParam, LPARAM lParam)
{
	EnableControl();
	return 0;
}
LRESULT CsteamcloudDlg::OnDisableControl(WPARAM wParam, LPARAM lParam)
{
	DisableControl();
	return 0;
}
void CsteamcloudDlg::EnableControl()
{
	download->EnableWindow(1);
	deletefile->EnableWindow(1);
	upload->EnableWindow(1);
	uploaddir->EnableWindow(1);
	refresh->EnableWindow(1);
	quota->ShowWindow(1);
	disconnect->EnableWindow(1);
}
void CsteamcloudDlg::DisableControl()
{
	download->EnableWindow(0);
	deletefile->EnableWindow(0);
	upload->EnableWindow(0);
	uploaddir->EnableWindow(0);
	refresh->EnableWindow(0);
	quota->ShowWindow(0);
	disconnect->EnableWindow(0);
}
LRESULT CsteamcloudDlg::OnClearList(WPARAM wParam, LPARAM lParam)
{
	Clearlist();
	return 0;
}
LRESULT CsteamcloudDlg::OnUpdateComboBox(WPARAM wParam, LPARAM lParam)
{
	UpdateAppIdHistoryFromInput();
	SaveComboBoxHistory();
	return 0;
}
LRESULT CsteamcloudDlg::OnShowAsyncMessage(WPARAM wParam, LPARAM lParam)
{
	AsyncMessagePayload* payload = reinterpret_cast<AsyncMessagePayload*>(lParam);
	if (payload != nullptr) {
		HWND target = ::IsWindow(this->m_hWnd) ? this->m_hWnd : NULL;
		::MessageBox(target, payload->text, payload->title, payload->flags);
		delete payload;
	}
	return 0;
}
LRESULT CsteamcloudDlg::OnRequestGetFiles(WPARAM wParam, LPARAM lParam)
{
	GetFiles();
	return 0;
}

void CsteamcloudDlg::PostAsyncMessage(const CString& title, const CString& text, UINT flags)
{
	AsyncMessagePayload* payload = new AsyncMessagePayload();
	payload->title = title;
	payload->text = text;
	payload->flags = flags;
	PostMessage(WM_SHOW_ASYNC_MESSAGE, 0, reinterpret_cast<LPARAM>(payload));
}

bool CsteamcloudDlg::TryBeginAction()
{
	bool expected = false;
	return active.compare_exchange_strong(expected, true);
}

void CsteamcloudDlg::EndAction()
{
	active.store(false);
}
void CsteamcloudDlg::UpdateQuota()
{
	uint64_t total = 0;
	uint64_t used = 0;
	uint64_t available = 0;
	{
		std::lock_guard<std::mutex> lock(m_dataMutex);
		total = m_quotaTotal;
		used = m_quotaUsed;
		available = m_quotaAvailable;
	}

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


void CsteamcloudDlg::RefreshListFromData()
{
	Clearlist();
	while(clearing)
	{
		Sleep(100); // Wait until the clearing operation is finished
	}
	std::lock_guard<std::mutex> lock(m_dataMutex);
	for (size_t index = 0; index < m_fileRowData.size(); ++index)
	{
		const FileRow& row = m_fileRowData[index];

		CString sizeStr, dateStr;

		switch (sizeunit) {
		case 0: sizeStr.Format(L"%d", row.size); break;
		case 1: sizeStr.Format(L"%.4f", (float)row.size / 1024); break;
		case 2: sizeStr.Format(L"%.4f", (float)row.size / (1024 * 1024)); break;
		default: sizeStr.Format(L"%d", row.size); break;
		}

		wchar_t dateBuf[100] = {};
		tm* timeinfo = localtime(&row.timestamp);
		wcsftime(dateBuf, sizeof(dateBuf) / sizeof(wchar_t), L"%e.%m.%Y %H:%M:%S", timeinfo);
		dateStr = dateBuf;

		int itemIndex = listfiles->InsertItem(static_cast<int>(index), row.name);
		listfiles->SetItemText(itemIndex, 1, dateStr);
		listfiles->SetItemText(itemIndex, 2, sizeStr);
		listfiles->SetItemText(itemIndex, 3, row.persisted ? L"true" : L"false");
		listfiles->SetItemText(itemIndex, 4, row.exists ? L"true" : L"false");
		listfiles->SetItemData(itemIndex, (DWORD_PTR)&m_fileRowData[index]);
	}

	if (m_nSortedColumn >= 0)
	{
		SortInfo info = { m_nSortedColumn, m_bSortAscending };
		listfiles->SortItems(CompareFunc, (LPARAM)&info);
	}
}


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
	m_hRequestPipe = INVALID_HANDLE_VALUE;
	m_hResponsePipe = INVALID_HANDLE_VALUE;
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
	//AfxInitRichEdit2();
	while (!m_RequestThreadRunning || !m_ResponseThreadRunning)
	{
		Sleep(10); // Wait for connection threads to start
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
	int starttime = GetTickCount64();
	int maxwait = 5000; // timeout time to wait for threads to finish
	while(active && ((GetTickCount64() - starttime) < maxwait))
	{
		Sleep(100); // Wait for finishing threads and actions
	}
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
			std::string response;
			if (SendCommandAndReadResponse("exit\n", 3000, response))
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
	m_RequestThreadEnabled = false;
	m_ResponseThreadEnabled = false;
	while(m_RequestThreadRunning)
	{
		Sleep(100); // Wait for threads to finish
	}
	while(m_ResponseThreadRunning)
	{
		Sleep(100); // Wait for threads to finish
	}
	{
		std::lock_guard<std::mutex> lock(m_pipeIoMutex);
		if (m_hRequestPipe && m_hRequestPipe != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_hRequestPipe);
			m_hRequestPipe = INVALID_HANDLE_VALUE;
		}
		if (m_hResponsePipe && m_hResponsePipe != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_hResponsePipe);
			m_hResponsePipe = INVALID_HANDLE_VALUE;
		}
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
	if (!TryBeginAction()) return;

	CString appidStr;
	inputappid->GetWindowTextW(appidStr);
	appidStr.Trim();
	if (appidStr.IsEmpty()) {
		::MessageBox(NULL,L"App ID is empty, please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		EndAction();
		return;
	}
	for (int i = 0; i < appidStr.GetLength(); ++i) {
		if (!isdigit(appidStr[i])) {
			::MessageBox(NULL,L"App ID must be a number!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}
	}
	int appid = _wtoi(appidStr);
	if (appid < 1) {
		::MessageBox(NULL,L"App ID must be greater than 0!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		EndAction();
		return;
	}

	thread([this, appid]() {
	PostMessage(WM_CLEAR_LIST); // Clear the list before connecting

	auto waitForPipeConnection = [&](ULONGLONG timeoutMs) -> bool {
		ULONGLONG start = GetTickCount64();
		while ((GetTickCount64() - start) < timeoutMs)
		{
			if (m_statusrequestpipe && m_statusresponsepipe)
			{
				return true;
			}
			Sleep(100);
		}
		return false;
	};

	auto probeWorkerStatus = [&]() -> bool {
		std::string statusResponse;
		if (!SendCommandAndReadResponse("status\n", 2000, statusResponse)) {
			return false;
		}
		try {
			auto parsed = json::parse(statusResponse);
			if (!parsed.is_array() || parsed.empty()) return false;
			std::string status = parsed[0].value("status", "");
			return !status.empty();
		}
		catch (...) {
			return false;
		}
	};

	auto rememberRunningWorkerHandle = [&]() {
		if (m_hWorkerProcess && m_hWorkerProcess != INVALID_HANDLE_VALUE) return;
		DWORD pid = GetProcessPIDByName(L"steam-worker.exe");
		if (pid != 0) {
			HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
			if (h != NULL && h != INVALID_HANDLE_VALUE) {
				m_hWorkerProcess = h;
			}
		}
	};

	auto waitForReadyAfterStart = [&](ULONGLONG timeoutMs) -> bool {
		if (!waitForPipeConnection(timeoutMs)) {
			return false;
		}
		std::string output;
		bool gotReady = false;
		{
			std::lock_guard<std::mutex> lock(m_pipeIoMutex);
			gotReady = ReadFromPipeWithTimeout(timeoutMs, output);
		}
		return gotReady && output == "READY";
	};

	auto startWorkerFromPath = [&](const CString& workerPath) -> bool {
		PROCESS_INFORMATION pi;
		STARTUPINFOW si = { sizeof(si) };
		if (!CreateProcessW(workerPath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
			return false;
		}
		if (m_hWorkerProcess && m_hWorkerProcess != INVALID_HANDLE_VALUE) {
			CloseHandle(m_hWorkerProcess);
		}
		m_hWorkerProcess = pi.hProcess;
		CloseHandle(pi.hThread);
		if (!waitForReadyAfterStart(10000)) {
			TerminateProcess(m_hWorkerProcess, 1);
			CloseHandle(m_hWorkerProcess);
			m_hWorkerProcess = NULL;
			return false;
		}
		return true;
	};

	bool workerAvailable = false;
	if (waitForPipeConnection(1500) && probeWorkerStatus()) {
		workerAvailable = true;
		rememberRunningWorkerHandle();
	}

	if (!workerAvailable)
	{
		WCHAR modulePath[MAX_PATH] = { 0 };
		if (GetModuleFileNameW(NULL, modulePath, MAX_PATH) > 0)
		{
			CString exePath(modulePath);
			int slash = exePath.ReverseFind(L'\\');
			if (slash > 0)
			{
				CString exeDir = exePath.Left(slash);
				CString localWorkerPath = exeDir + L"\\steam-worker.exe";
				if (PathFileExistsW(localWorkerPath))
				{
					workerAvailable = startWorkerFromPath(localWorkerPath);
				}
			}
		}
	}

	if (!workerAvailable)
	{
		WCHAR tempPath[MAX_PATH];
		if (!GetEnvironmentVariableW(L"TEMP", tempPath, MAX_PATH)) {
			PostAsyncMessage(L"Error", L"Cannot get TEMP path.", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}
		CString workerPath = CString(tempPath) + L"\\steam-worker.exe";

		if (PathFileExistsW(workerPath)) {
			DeleteFileW(workerPath);
		}
		if (!ExtractResourceToFile(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_WORKER), RT_RCDATA, workerPath)) {
			PostAsyncMessage(L"Error", L"Unable to extract steam-worker.exe!", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}
		workerAvailable = startWorkerFromPath(workerPath);
	}

	if (!workerAvailable)
	{
		PostAsyncMessage(L"Error", L"Unable to start or connect to steam-worker.", MB_OK | MB_ICONERROR | MB_TOPMOST);
		EndAction();
		return;
	}

	if (!waitForPipeConnection(4000) || !probeWorkerStatus())
	{
		PostAsyncMessage(L"Error", L"Worker pipe is connected, but status check failed.", MB_OK | MB_ICONERROR | MB_TOPMOST);
		EndAction();
		return;
	}

	Sleep(400); // Ensure the worker is ready to receive commands
	std::wstring statusMessage;
	if (!EnsureWorkerIdle(statusMessage)) {
		PostAsyncMessage(L"Worker busy", statusMessage.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
		EndAction();
		return;
	}
	// Send the connect command to the worker with the specified AppID
	std::string cmd = "connect " + std::to_string(appid) + "\n";
	string response;
	if (!SendCommandAndReadResponse(cmd, 5000, response)) {
		PostAsyncMessage(L"Error", L"Can't write to pipe.", MB_OK | MB_ICONERROR | MB_TOPMOST);
		EndAction();
		return;
	}

	try {
		auto parsed = json::parse(response);
		if (parsed.is_array() && parsed.size() > 0) {
			std::string status = parsed[0].value("status", "");

			if (status == "connected") {
				
				PostMessage(WM_ENABLE_CONTROL, 0, 0); // Enable controls after successful connection
				PostMessage(WM_UPDATE_COMBOBOX, 0, 0); // Update the AppID combo box
				init = true;
				EndAction();
				PostMessage(WM_REQUEST_GETFILES, 0, 0);
				return;
			}
			else if (status == "SteamAPI_Init failed" || status == "SteamAPI not initialized") {
				
				PostMessage(WM_DISABLE_CONTROL, 0, 0); // Disable controls if initialization failed
				PostMessage(WM_CLEAR_LIST, 0, 0); // Clear the file list
				init = false;
				EndAction();
				PostAsyncMessage(L"SteamAPI Error", CString(CA2W(status.c_str())), MB_OK | MB_ICONERROR | MB_TOPMOST);
				return;
			}
		}
		// If we reach here, it means the response is not in the expected format
		PostMessage(WM_DISABLE_CONTROL, 0, 0); // Disable controls if initialization failed
		PostMessage(WM_CLEAR_LIST, 0, 0); // Clear the file list
		init = false;
		EndAction();
		PostAsyncMessage(L"Worker Response", CString(CA2W(response.c_str())), MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	catch (const std::exception& e) {
		PostMessage(WM_DISABLE_CONTROL, 0, 0); // Disable controls if initialization failed
		PostMessage(WM_CLEAR_LIST, 0, 0); // Clear the file list
		init = false;
		CString msg;
		msg.Format(L"Error when parsing JSON: %S", e.what());
		EndAction();
		PostAsyncMessage(L"Error", msg, MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	}).detach(); // Detach the thread to allow it to run independently
}


void CsteamcloudDlg::OnBnClickedDelete()
{
	if (!TryBeginAction()) return;

	if(!init)
	{
		::MessageBox(NULL,L"Please connect to Steam first!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		EndAction();
		return;
	}

	std::vector<std::string> selectedNames;
	{
		int files[25] = { -1 };
		fill_n(files, 25, -1);
		int count = 0;
		int mark = -1;
		POSITION pos = listfiles->GetFirstSelectedItemPosition();
		if (pos == NULL)
		{
			::MessageBox(NULL,L"No file is selected! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		mark = listfiles->GetNextSelectedItem(pos);
		while (mark != -1)
		{
			if (count == 25) break;
			files[count++] = mark;
			mark = listfiles->GetNextSelectedItem(pos);
		}

		for (int i = 0; i < count; ++i)
		{
			CString nameW = listfiles->GetItemText(files[i], 0);
			CT2A nameA(nameW);
			selectedNames.emplace_back(nameA);
		}
	}

	thread([this, selectedNames]() {
		std::wstring statusMessage;
		if (!EnsureWorkerIdle(statusMessage)) {
			PostAsyncMessage(L"Worker busy", statusMessage.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
			EndAction();
			return;
		}

		std::ostringstream cmd;
		cmd << "delete ";
		for (size_t i = 0; i < selectedNames.size(); ++i)
		{
			cmd << selectedNames[i];
			if (i + 1 < selectedNames.size()) cmd << ";";
		}

		std::string output;
		if (!SendCommandAndReadResponse(cmd.str(), 5000, output))
		{
			PostAsyncMessage(L"ERROR", L"Failed to send delete command to worker!", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		CString result;
		try {
			auto j = json::parse(output);

			if (j.is_array() && j.empty()) {
				EndAction();
				return;
			}
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
			PostAsyncMessage(L"ERROR", err, MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}
		EndAction();
		PostMessage(WM_CLEAR_LIST, 0, 0);
		PostMessage(WM_REQUEST_GETFILES, 0, 0);
		PostAsyncMessage(L"Delete result", result, MB_OK | MB_TOPMOST);
	}).detach();
}


void CsteamcloudDlg::OnBnClickedUpload()
{
	if (!TryBeginAction()) return;
	if (!init)
	{
		::MessageBox(NULL,L"Please connect to Steam first!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		EndAction();
		return;
	}

	CString buffer;
	const DWORD size = MAX_UNICODE_PATH + (MAX_PATH * MAX_FILES_COUNT);
	buffer.GetBufferSetLength(size);
	CFileDialog dlg(TRUE, NULL, NULL,
		OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST,
		L"All Files (*.*)|*.*||", this);
	OPENFILENAMEW& ofn = dlg.GetOFN();
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = this->m_hWnd;
	ofn.lpstrFile = buffer.GetBuffer();
	ofn.nMaxFile = size;
	if (dlg.DoModal() != IDOK) {
		EndAction();
		return;
	}

	std::vector<std::pair<std::string, std::string>> entries;
	POSITION pos = dlg.GetStartPosition();
	while (pos) {
		CString fullPath = dlg.GetNextPathName(pos);
		int slash = fullPath.ReverseFind(L'\\');
		CString fileName = (slash >= 0) ? fullPath.Mid(slash + 1) : fullPath;
		std::string cloudName = CT2A(fileName);
		std::string localPath = CT2A(fullPath);
		entries.emplace_back(cloudName, localPath);
		if (entries.size() >= MAX_FILES_COUNT) break;
	}

	if (entries.empty()) {
		EndAction();
		return;
	}

	thread([this, entries]() {
		std::wstring statusMessage;
		if (!EnsureWorkerIdle(statusMessage)) {
			PostAsyncMessage(L"Worker busy", statusMessage.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
			EndAction();
			return;
		}

		std::ostringstream uploadCommand;
		uploadCommand << "upload ";
		for (size_t i = 0; i < entries.size(); ++i) {
			uploadCommand << entries[i].first << "," << entries[i].second;
			if (i + 1 < entries.size()) uploadCommand << ";";
		}

		std::string output;
		if (!SendCommandAndReadResponse(uploadCommand.str(), 10000, output))
		{
			PostAsyncMessage(L"Error", L"Unable to write to pipe!", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

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
			PostAsyncMessage(L"Error", L"Answer from worker is not valid JSON!", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		EndAction();
		if (!msg.IsEmpty()) {
			PostAsyncMessage(L"Upload results", msg, MB_OK | MB_TOPMOST);
		}
		PostMessage(WM_REQUEST_GETFILES, 0, 0);
	}).detach();
}



void CsteamcloudDlg::OnBnClickedDirupload()
{
	if (!TryBeginAction()) return;
	if (!init)
	{
		::MessageBox(NULL,L"Please connect to Steam first!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		EndAction();
		return;
	}

	prompt promptDialog(this);
	if (promptDialog.DoModal() != IDOK) {
		EndAction();
		return;
	}

	CString cloudPrefix = returned == nullptr ? L"" : returned;
	cloudPrefix.Trim();
	cloudPrefix.Replace(L"\\", L"/");
	while (!cloudPrefix.IsEmpty() && cloudPrefix.Right(1) == L"/") {
		cloudPrefix = cloudPrefix.Left(cloudPrefix.GetLength() - 1);
	}

	CString buffer;
	const DWORD size = MAX_UNICODE_PATH + (MAX_PATH * MAX_FILES_COUNT);
	buffer.GetBufferSetLength(size);
	CFileDialog dlg(TRUE, NULL, NULL,
		OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST,
		L"All Files (*.*)|*.*||", this);
	OPENFILENAMEW& ofn = dlg.GetOFN();
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = this->m_hWnd;
	ofn.lpstrFile = buffer.GetBuffer();
	ofn.nMaxFile = size;
	if (dlg.DoModal() != IDOK) {
		EndAction();
		return;
	}

	std::vector<std::pair<std::string, std::string>> entries;
	POSITION pos = dlg.GetStartPosition();
	while (pos) {
		CString fullPath = dlg.GetNextPathName(pos);
		int slash = fullPath.ReverseFind(L'\\');
		CString cloudName = (slash >= 0) ? fullPath.Mid(slash + 1) : fullPath;
		if (!cloudPrefix.IsEmpty()) {
			cloudName = cloudPrefix + L"/" + cloudName;
		}
		entries.emplace_back(std::string(CT2A(cloudName)), std::string(CT2A(fullPath)));
		if (entries.size() >= MAX_FILES_COUNT) break;
	}

	if (entries.empty()) {
		EndAction();
		return;
	}

	thread([this, entries]() {
		std::wstring statusMessage;
		if (!EnsureWorkerIdle(statusMessage)) {
			PostAsyncMessage(L"Worker busy", statusMessage.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
			EndAction();
			return;
		}

		std::ostringstream uploadCommand;
		uploadCommand << "upload ";
		for (size_t i = 0; i < entries.size(); i++)
		{
			uploadCommand << entries[i].first << "," << entries[i].second;
			if (i + 1 < entries.size()) uploadCommand << ";";
		}

		std::string response;
		if (!SendCommandAndReadResponse(uploadCommand.str(), 10000, response))
		{
			PostAsyncMessage(L"Error", L"Unable to write to pipe!", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		try
		{
			json parsed = json::parse(response);
			CString output;

			for (auto& entry : parsed)
			{
				CString name;
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
					output += L": uploaded (" + CString(std::to_wstring(entry["size"].get<int>()).c_str()) + L" bytes)";
				}

				output += L"\r\n";
			}

			if (!output.IsEmpty())
				PostAsyncMessage(L"Upload result", output, MB_OK | MB_TOPMOST);
		}
		catch (...)
		{
			PostAsyncMessage(L"ERROR", L"Failed to parse response", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		EndAction();
		PostMessage(WM_REQUEST_GETFILES, 0, 0);
	}).detach();
}

void CsteamcloudDlg::OnBnClickedDownload()
{
	if (!TryBeginAction()) return;
	if (!init)
	{
		::MessageBox(NULL,L"Please connect to Steam first!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		EndAction();
		return;
	}

	constexpr int MAX_SELECTED = 10;
	int selectedIndices[MAX_SELECTED];
	int count = 0;
	POSITION pos = listfiles->GetFirstSelectedItemPosition();
	while (pos && count < MAX_SELECTED)
	{
		int index = listfiles->GetNextSelectedItem(pos);
		selectedIndices[count++] = index;
	}

	if (count == 0)
	{
		::MessageBox(NULL,L"No file is selected! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		EndAction();
		return;
	}

	std::vector<CString> cloudPaths;
	std::vector<CString> outputPaths;
	CString targetDirectory;

	if (count == 1)
	{
		CString cloudPath = listfiles->GetItemText(selectedIndices[0], 0);
		CString fileName = cloudPath.Mid(cloudPath.ReverseFind(L'/') + 1);
		CFileDialog filedialog(FALSE, NULL, fileName, OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, L"All Files (*.*)|*.*||");
		if (filedialog.DoModal() != IDOK) {
			EndAction();
			return;
		}
		cloudPaths.push_back(cloudPath);
		outputPaths.push_back(filedialog.GetPathName());
	}
	else
	{
		CFolderPickerDialog dialog(NULL, OFN_EXPLORER | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST | OFN_CREATEPROMPT, this);
		if (dialog.DoModal() != IDOK) {
			EndAction();
			return;
		}
		targetDirectory = dialog.GetFolderPath();
		for (int i = 0; i < count; ++i)
		{
			CString cloudPath = listfiles->GetItemText(selectedIndices[i], 0);
			cloudPaths.push_back(cloudPath);
		}
	}

	thread([this, cloudPaths, outputPaths, targetDirectory, count]() {
		std::wstring statusMessage;
		if (!EnsureWorkerIdle(statusMessage)) {
			PostAsyncMessage(L"Worker busy", statusMessage.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
			EndAction();
			return;
		}

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

		std::string response;
		if (!SendCommandAndReadResponse(ss.str(), 10000, response))
		{
			PostAsyncMessage(L"Error", L"Unable to write to pipe", MB_OK | MB_ICONERROR | MB_TOPMOST);
			EndAction();
			return;
		}

		try
		{
			json result = json::parse(response);
			if (result.is_array() && result.empty()) {
				EndAction();
				return;
			}
			CString statusMsg;
			for (const auto& item : result)
			{
				CString name = CA2W(item.value("name", "").c_str());
				CString status = CA2W(item.value("status", "").c_str());
				int size = item.value("size", 0);

				if (status.IsEmpty())
				{
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
					statusMsg += name + L": " + status + L" (" + CString(std::to_wstring(size).c_str()) + L" bytes)\r\n";
				}
			}

			PostAsyncMessage(L"Download status", statusMsg, MB_OK | MB_TOPMOST);
		}
		catch (...)
		{
			PostAsyncMessage(L"ERROR", L"Error on parsing JSON response.", MB_OK | MB_ICONERROR | MB_TOPMOST);
		}
		EndAction();
	}).detach();
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
	Clearlist(); // Clear the list before refreshing
	if (!init)
	{
		// this should not happen, but just in case
		::MessageBox(NULL,L"Please connect to Steam first!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		return;
	}
	GetFiles();
}

void CsteamcloudDlg::OnBnClickedDisconnect()
{
	if (!TryBeginAction()) return;
	thread([this]() {
		if (!init) {
			EndAction();
			return;
		}

		std::wstring statusMessage;
		if (!EnsureWorkerIdle(statusMessage)) {
			PostAsyncMessage(L"Worker busy", statusMessage.c_str(), MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
			EndAction();
			return;
		}

		if (m_hWorkerProcess && m_hRequestPipe)
		{
			std::string response;
			if (!SendCommandAndReadResponse("exit\n", 3000, response)) {
				PostAsyncMessage(L"Error", L"Unable to write to pipe (exit).", MB_OK | MB_ICONERROR | MB_TOPMOST);
			}
			else {
				Sleep(200);
				DWORD result = WaitForSingleObject(m_hWorkerProcess, 0);
				if (result == WAIT_TIMEOUT)
				{
					if (!TerminateProcess(m_hWorkerProcess, 1)) {
						PostAsyncMessage(L"Error", L"The worker process cannot be terminated manually.", MB_OK | MB_ICONERROR | MB_TOPMOST);
					}
				}
			}
		}

		PostAsyncMessage(L"Info", L"Disconnected from Steam Cloud.", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
		init = false;
		if (m_hWorkerProcess)
		{
			CloseHandle(m_hWorkerProcess);
			m_hWorkerProcess = NULL;
		}
		PostMessage(WM_DISABLE_CONTROL, 0, 0);
		PostMessage(WM_CLEAR_LIST, 0, 0);
		EndAction();
	}).detach();
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
	std::map<CString, int64_t> sizesCopy;
	uint64_t quotaUsed = 0;
	uint64_t quotaTotal = 0;
	{
		std::lock_guard<std::mutex> lock(m_dataMutex);
		sizesCopy = m_fileSizesBytes;
		quotaUsed = m_quotaUsed;
		quotaTotal = m_quotaTotal;
	}

	int rowCount = listfiles->GetItemCount();

	for (int i = 0; i < rowCount; ++i)
	{
		CString filename = listfiles->GetItemText(i, 0);

		auto it = sizesCopy.find(filename);
		if (it != sizesCopy.end())
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
		quotaText.Format(L"%llu/%llu Bytes used", quotaUsed, quotaTotal);
		break;
	case 1:
		quotaText.Format(L"%.4f/%.4f KB used", (float)quotaUsed / 1024, (float)quotaTotal / 1024);
		break;
	case 2:
		quotaText.Format(L"%.4f/%.4f MB used", (float)quotaUsed / (1024 * 1024), (float)quotaTotal / (1024 * 1024));
		break;
	default:
		quotaText.Format(L"%llu/%llu Bytes used", quotaUsed, quotaTotal);
		break;
	}
	SetDlgItemTextW(IDC_QUOTA, quotaText);
}
void CsteamcloudDlg::RequestPipeThread()
{
	while (m_RequestThreadEnabled)
	{
		m_RequestThreadRunning = true;
		m_RequestThreadWaiting = true;
		HANDLE requestPipe = CreateFileW(
			L"\\\\.\\pipe\\SteamDlgRequestPipe",
			GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			0,
			NULL);
		if (requestPipe == INVALID_HANDLE_VALUE)
		{
			Sleep(100);
			continue;
		}
		{
			std::lock_guard<std::mutex> lock(m_pipeIoMutex);
			if (m_hRequestPipe && m_hRequestPipe != INVALID_HANDLE_VALUE) {
				CloseHandle(m_hRequestPipe);
			}
			m_hRequestPipe = requestPipe;
		}
		m_RequestThreadWaiting = false;
		m_statusrequestpipe = true;

		while (m_RequestThreadEnabled)
		{
			const char* ping = ".\n";
			DWORD written = 0;
			BOOL ok = TRUE;
			if (m_pipeIoMutex.try_lock())
			{
				ok = WriteFile(m_hRequestPipe, ping, (DWORD)strlen(ping), &written, NULL);
				m_pipeIoMutex.unlock();
			}
				 
			if (!ok) {
				DWORD err = GetLastError();
				if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED || err == ERROR_NO_DATA || err == ERROR_INVALID_HANDLE) {
					{
						std::lock_guard<std::mutex> lock(m_pipeIoMutex);
						if (m_hRequestPipe && m_hRequestPipe != INVALID_HANDLE_VALUE) {
							CloseHandle(m_hRequestPipe);
							m_hRequestPipe = INVALID_HANDLE_VALUE;
						}
					}
					m_statusrequestpipe = false;
					break;
				}
			}

			Sleep(800);
		}
	}
	m_RequestThreadRunning = false;
}
void CsteamcloudDlg::ResponsePipeThread()
{
	while (m_ResponseThreadEnabled)
	{
		m_ResponseThreadRunning = true;
		m_ResponseThreadWaiting = true;
		HANDLE responsePipe = CreateFileW(
			L"\\\\.\\pipe\\SteamDlgResponsePipe",
			GENERIC_READ,
			0,
			NULL,
			OPEN_EXISTING,
			0,
			NULL);
		if (responsePipe == INVALID_HANDLE_VALUE)
		{
			Sleep(100);
			continue;
		}
		{
			std::lock_guard<std::mutex> lock(m_pipeIoMutex);
			if (m_hResponsePipe && m_hResponsePipe != INVALID_HANDLE_VALUE) {
				CloseHandle(m_hResponsePipe);
			}
			m_hResponsePipe = responsePipe;
		}
		m_ResponseThreadWaiting = false;
		m_statusresponsepipe = true;

		while (m_ResponseThreadEnabled)
		{
			if (m_pipeIoMutex.try_lock())
			{
				DWORD available = 0;
				BOOL ok = PeekNamedPipe(m_hResponsePipe, NULL, 0, NULL, &available, NULL);
				m_pipeIoMutex.unlock();
				if (!ok)
				{
					DWORD err = GetLastError();
					if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED || err == ERROR_NO_DATA || err == ERROR_INVALID_HANDLE)
					{
						std::lock_guard<std::mutex> lock(m_pipeIoMutex);
						if (m_hResponsePipe && m_hResponsePipe != INVALID_HANDLE_VALUE) {
							CloseHandle(m_hResponsePipe);
							m_hResponsePipe = INVALID_HANDLE_VALUE;
						}
						m_statusresponsepipe = false;
						break;
					}
				}
			}
			Sleep(300);
		}
	}
	m_ResponseThreadRunning = false;
}

