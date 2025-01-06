
// hasher2Dlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "steamcloud.h"
#include "steamcloudDlg.h"
#include "afxdialogex.h"
#include "afxwin.h"
#include "prompt.h"
//#include <thread>
#include <fstream>
#include <Windows.h>
template<class TYPE>
bool RegSetKey(HKEY key, LPSTR keyloc, unsigned long type, REGSAM access, LPSTR name, TYPE indatax);
int RegCrtKey(HKEY key, LPSTR keyloc, REGSAM access);
template<class TYPE>
int RegGetKey(HKEY key, LPSTR keyloc, unsigned long type, REGSAM access, LPSTR name, TYPE outdatax);
DWORD g_BytesTransferred = 0;
void CsteamcloudDlg::Clearlist()
{
	int count = listfiles->GetItemCount();
	for (int i = 0; i < count; i++)
	{
		
		int test = listfiles->DeleteItem(0);
	}
}
void CsteamcloudDlg::GetFiles()
{
	char *file;
	int size=0;
	int32 count = 0;
	int64 buff=0;
	count = SteamRemoteStorage()->GetFileCount();
	for (int i = 0; i < count; i++)
	{
		bool existperst=false;
		wchar_t buf[200] = {};
		wchar_t mem[500] = {};
		file = (char*)SteamRemoteStorage()->GetFileNameAndSize(i, &size);
		mbstowcs(mem, file, strlen(file));
		swprintf_s(buf, L"%s", mem);
		listfiles->InsertItem(i,buf);
		switch (sizeunit)
		{
			case 0:
			{
				swprintf_s(buf, L"%d", size);
				break;
			}
			case 1:
			{
				float fsize = (float)size / 1024;
				swprintf_s(buf, L"%.4f", fsize);
				break;
			}
			case 2:
			{
				float fsize = (float)size / (1024*1024);
				swprintf_s(buf, L"%.4f", fsize);
				break;
			}
			default:
			{
				swprintf_s(buf, L"%d", size);
				break;
			}
		}
		listfiles->SetItemText(i, 2, buf);
		buff=SteamRemoteStorage()->GetFileTimestamp(file);
		swprintf_s(buf, L"%lli", buff);
		tm *timeinfo;
		timeinfo = localtime((time_t*)&buff);
		wcsftime(buf,sizeof(buf)/2,L"%e.%m.%G %H:%M:%I",timeinfo);
		listfiles->SetItemText(i, 1, buf);
		existperst = SteamRemoteStorage()->FileExists(file);
		switch (existperst)
		{
			case true:
			{
				listfiles->SetItemText(i, 4, L"true");
				break;
			}
			case false:
			{
				listfiles->SetItemText(i, 4, L"false");
				break;
			}
		}
		existperst = SteamRemoteStorage()->FilePersisted(file);
		switch (existperst)
		{
			case true:
			{
				listfiles->SetItemText(i, 3, L"true");
				break;
			}
			case false:
			{
				listfiles->SetItemText(i, 3, L"false");
				break;
			}
		}
		uint64 total=0, used=0,available=0;
		SteamRemoteStorage()->GetQuota(&total, &available);
		used = total - available;
		switch (sizeunit)
		{
			case 0:
			{
				swprintf_s(buf, L"%llu/%llu of Bytes used", used, total);
				break;
			}
			case 1:
			{
				float fused = (float)used / 1024;
				float ftotal = (float)total/1024;
				swprintf_s(buf, L"%.4f/%.4f KB used", fused, ftotal);
				break;
			}
			case 2:
			{
				float fused = (float)used / (1024 * 1024);
				float ftotal = (float)total / (1024*1024);
				swprintf_s(buf, L"%.4f/%.4f MB used", fused, ftotal);
				break;
			}
			default:
			{
				swprintf_s(buf, L"%llu/%llu of Bytes used", used, total);
				break;
			}
		}
		SetDlgItemTextW(IDC_QUOTA, buf);
	}
	

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


//#ifdef _DEBUG
//#define new DEBUG_NEW
//#endif
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
template<class TYPE>
bool RegSetKey(HKEY key,LPSTR keyloc,unsigned long type, REGSAM access,LPSTR name,TYPE indatax)
{
	unsigned long size = sizeof(type);
	char errorbuf[200];
	HKEY keyval;
	bool onerr = 1;
	int err;
	err = RegOpenKeyExA(key, keyloc, NULL, access, &keyval);
	if (err != ERROR_SUCCESS)
	{
		sprintf_s(errorbuf, "%i\n", err);
		onerr = 0;
		ASSERT(errorbuf);
	} else if(err == ERROR_SUCCESS)
	{ 
		err = RegSetValueExA(keyval, name, NULL, type, (BYTE*)indatax, size);
		if (err != ERROR_SUCCESS)
		{
			sprintf_s(errorbuf, "%i\n", err);
			onerr = 0;
			ASSERT(errorbuf);
		}
	}
	
		CloseHandle(keyval);
	return onerr;
}
int RegCrtKey(HKEY key, LPSTR keyloc, REGSAM access)
{
	HKEY keyval;
	int err;
	char errorbuf[200];
	DWORD dispvalue;
	err = RegCreateKeyExA(key, keyloc, NULL, NULL, REG_OPTION_NON_VOLATILE, access,NULL, &keyval, &dispvalue);
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
template<class TYPE>
int RegGetKey(HKEY key, LPSTR keyloc, unsigned long type, REGSAM access, LPSTR name, TYPE outdatax)
{
	unsigned long size = sizeof(type);
	char errorbuf[200];
	HKEY keyval;
	int onerr = 0;
	int err;
	err = RegOpenKeyExA(key, keyloc, NULL, access, &keyval);
	if (err != ERROR_SUCCESS)
	{
		onerr = false;
	}
	err = RegQueryValueExA(keyval, name, NULL, &type, (BYTE*)outdatax, &size);
	switch (err)
	{
	case ERROR_FILE_NOT_FOUND:
	{
		onerr = 2;
		break;
	} 
	case ERROR_MORE_DATA:
	{
		onerr = 3;
		break;
	}
	case ERROR_SUCCESS:
	{
		onerr = 1;
		break;
	}
	default:
	{
		sprintf_s(errorbuf, "%i\n", err);
		ASSERT(errorbuf);
		onerr = 0;
		break;
	}
	}
		CloseHandle(keyval);
	return onerr;
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

	//AfxInitRichEdit2();
	
	CsteamcloudDlg::ShowWindow(SW_SHOW);
	CsteamcloudDlg::RedrawWindow();
	CsteamcloudDlg::CenterWindow();
	CDialog::OnInitDialog();
	out = RegCrtKey(HKEY_CURRENT_USER, "Software\\steamcloud", KEY_ALL_ACCESS | KEY_WOW64_64KEY);
	if (out == 1)
	{
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayEnable", &indata);
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", &indata);
	}
	else if(out == 0)
	{
		trayenable = 1;
		minimizeen = 1;
		m_bMinimizeToTray = TRUE;
	}
	else if (out == 2)
	{
		out = RegGetKey(HKEY_CURRENT_USER, "Software\\steamcloud", NULL, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", &outdata);
		if (out == 2)
		{
			
			RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", &indata);
			
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
		out = RegGetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayEnable", &outdata);
		if (out == 2)
		{

			RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", &indata);
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
	}
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	TraySetIcon(m_hIcon);
	TraySetToolTip(L"Steamcloud");
	TraySetMenu(ID_MENU_MINIMIZE);
	
	if (CsteamcloudDlg::IsWindowVisible() != 0)
	{
		
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
		sizeunit = 0;
		Bytes->SetCheck(BST_CHECKED);
		Kbytes->SetCheck(BST_UNCHECKED);
		Mbytes->SetCheck(BST_UNCHECKED);
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
	if (init)
	{
		SteamAPI_Shutdown();
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
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", &indata);
		m_bMinimizeToTray = TRUE;
		minimizeen = true;
	}
	else
	{
		indata = 0;
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayMinimize", &indata);
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
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayEnable", &indata);
		trayenable = true;
		checkbox->EnableWindow();
		TrayShow();
	}
	else
	{
		indata = 0;
		RegSetKey(HKEY_CURRENT_USER, "Software\\steamcloud", REG_DWORD, KEY_ALL_ACCESS | KEY_WOW64_64KEY, "TrayEnable", &indata);
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
	if (init)
	{
		SteamAPI_Shutdown();
		init = false;
	}
	bool closed = false;
	int appid=-1;
	appid = GetDlgItemInt(IDC_APPID);
	if (appid < 1)
	{
		MessageBox( L"App ID is empty or negative number, please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
	}
	else
	{
		
		int argc=0;
		PCHAR* argv;
		char dir[500] = {}, file[500] = {};
		char drive[5], dirs[500],x[30];
		char buf[150] = {}, check[150] = {};
		sprintf_s(buf, "%d", appid);
		SetEnvironmentVariableA("SteamAppID", buf);
		init = SteamAPI_Init();
		
		if (init)
		{
			//steam.Init();
			//steam = SteamClient();
			//steampipe = SteamClient()->CreateSteamPipe();
			//steamuser = SteamClient()->ConnectToGlobalUser(steampipe);
			GetFiles();
			deletefile->EnableWindow();
			upload->EnableWindow();
			uploaddir->EnableWindow();
			download->EnableWindow();
			refresh->EnableWindow();
			quota->ShowWindow(1);
			disconnect->EnableWindow();

		}
		else
		{ 

		argv = CommandLineToArgvA(GetCommandLineA(), &argc);
		_splitpath_s(argv[0], drive, dirs,x,x);
		
		sprintf_s(dir, "%s%s", drive, dirs);
		sprintf_s(file, "%ssteam_appid.txt", dir);
		HANDLE fileapp;
		fileapp = CreateFileA(file, GENERIC_READ | GENERIC_WRITE,0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (fileapp != INVALID_HANDLE_VALUE)
		{
			OVERLAPPED ol = {0};
			
			
			WriteFile(fileapp, &buf, (ULONG)strlen(buf), NULL, NULL);
			Sleep(300);
			if(ReadFileEx(fileapp, check,150,&ol, FileIOCompletionRoutine) != FALSE)
			{ 
				Sleep(300);

				if (!strcmp(buf, check))
				{
					if (!closed) { CloseHandle(fileapp); closed = true; }
					init = SteamAPI_Init();
					if (init)
					{
						//steam.Init();
						GetFiles();
						deletefile->EnableWindow();
						upload->EnableWindow();
						uploaddir->EnableWindow();
						download->EnableWindow();
						refresh->EnableWindow();
						quota->ShowWindow(1);
						disconnect->EnableWindow();
					}

				}
				else
				{
					MessageBox(L"Unknown Error!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
				}
			}
			else
			{
				MessageBox( L"Error READ FILE!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
			}
			if (!closed) { CloseHandle(fileapp); closed = true; }
		}
		else
		{
			MessageBox(L"Error opening file!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		}
		}
	}
	
}


void CsteamcloudDlg::OnBnClickedDelete()
{
	bool filedeleted = false;
	int files[25] = { -1 };
	fill_n(files, 25, -1);
	int count = 0;
	int mark = -1;
	POSITION pos = listfiles->GetFirstSelectedItemPosition();
	if (pos == NULL)
	{
		MessageBox(L"No file is selected! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST); 
		goto end;
	}
	mark = listfiles->GetNextSelectedItem(pos);
	while (mark != -1)
	{
		if (count == 25) break; // break if we've reached the maximum number of selected items
		files[count] = mark; // save the selected item index
		count++; // increment the count
		mark = listfiles->GetNextSelectedItem(pos);
	}
	filedeleted = true;
	for (int i = 0; i < count; i++)
	{
		CT2A file(listfiles->GetItemText(files[i], 0));
		if (SteamRemoteStorage()->FileDelete(file.m_psz) == false)
		{
			filedeleted = false;
		}
		Sleep(300); // give steam time to process the deletion request
	}
	if (!filedeleted)
	{
		MessageBox(L"Failed to delete some file/files!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
	}
	else
	{
		MessageBox(L"File/Files Succesfully deleted!", L"INFO", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
	}
	
end:
	Clearlist();
	GetFiles();
}

void CsteamcloudDlg::OnBnClickedUpload()
{
	bool fileopened[MAX_FILES_COUNT] = { false };
	bool filesize[MAX_FILES_COUNT] = { false };
	wchar_t filelist[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t filename[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t dirname[MAX_PATH] = { 0 };
	int filecount = 0;
	CString filenames;
	DWORD maxfiles = 10;
	fstream file;
	wchar_t* pFile = nullptr;
	wchar_t szFilter[] = L"All Files (*.*)|*.*||";
	CFileDialog dlg(TRUE, NULL, NULL, OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST, szFilter, this);
	OPENFILENAMEW& ofn = dlg.GetOFN();
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = this->m_hWnd;
	//ofn.Flags += OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST;
	DWORD size = MAX_UNICODE_PATH + (MAX_PATH * MAX_FILES_COUNT);
	filenames.GetBufferSetLength(size);
	int len = filenames.GetLength();
	ofn.lpstrFile = filenames.GetBuffer();
	ofn.nMaxFile = size;
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
					sizeof(CString[MAX_PATH]);
					wcscpy_s(filename[i], dlg.GetFileName().GetString());
					wcscpy_s(dirname, dlg.GetFolderPath().GetString());
					Sleep(1);
				}
				break;
			}
			filecount++;			
			swprintf_s(filelist[i], L"%s\\%s", dirname, &filenames.GetBuffer()[prevpos + 1]);
			swprintf_s(filename[i], L"%s", &filenames.GetBuffer()[prevpos + 1]);
		}
		ZeroMemory(returned, sizeof(returned));
		for (int i = 0; i < filecount; i++)
		{
			file.open(filelist[i], ios::in | ios::binary);
			if (file.is_open())
			{
				ULONG length = 0;
				fileopened[i] = true;
				file.seekg(0, file.end);
				length = file.tellg();
				file.seekg(0, file.beg);
				if (length >= 102400000)
				{
					filesize[i] = false;
					continue;
				}
				filesize[i] = true;
				vector<std::byte> buffer(length);
				char filenamex[MAX_FILES_COUNT][MAX_PATH] = { 0 };
				wcstombs(filenamex[i], filename[i], sizeof(filenamex[i]));
				file.read(reinterpret_cast<char*>(buffer.data()), length);
				if (SteamRemoteStorage()->FileWrite(filenamex[i], reinterpret_cast<char*>(buffer.data()), length) == false)
				{
					MessageBox(L"Error uploading file!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
				}
				file.close();
				Sleep(300);
			}
		}
	}
	Clearlist();
	GetFiles();
	Sleep(1);
}


void CsteamcloudDlg::OnBnClickedDirupload()
{
	bool emptydir = true;
	prompt dialog(this);
	bool fileopened[MAX_FILES_COUNT] = {false};
	bool filesize[MAX_FILES_COUNT] = { false };
	wchar_t filelist[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t filename[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t dirname[MAX_PATH] = { 0 };
	wchar_t dirupload[MAX_PATH] = { 0 };
	int filecount = 0;
	CString filenames;
	DWORD maxfiles = 10;
	fstream file;
	wchar_t* pFile = nullptr;
	wchar_t szFilter[] = L"All Files (*.*)|*.*||";
	CFileDialog dlg(TRUE, NULL, NULL, OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST, szFilter, this);
	OPENFILENAMEW& ofn = dlg.GetOFN();
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = this->m_hWnd;
	//ofn.Flags += OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST;
	DWORD size = MAX_UNICODE_PATH + (MAX_PATH * MAX_FILES_COUNT);
	filenames.GetBufferSetLength(size);
	int len = filenames.GetLength();
	ofn.lpstrFile = filenames.GetBuffer();
	ofn.nMaxFile = size;
	
	if (dialog.DoModal() == IDOK)
	{
		if (wcscmp(returned, L"") != 0)
		{
			wcscpy_s(dirupload, returned);
			emptydir = true;
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
						sizeof(CString[MAX_PATH]);
						wcscpy_s(filename[i], dlg.GetFileName().GetString());
						if (emptydir)
						{
							wstring tmpbuf; 
							tmpbuf = filename[i];
							swprintf_s(filelist[i], L"%s\\%s", dlg.GetFolderPath().GetString(), dlg.GetFileName().GetString());
							swprintf_s(filename[i], L"%s/%s", dirupload, tmpbuf.c_str());
						}
						else
						{
							wcscpy_s(dirname, dlg.GetFolderPath().GetString());
						}
						
						Sleep(1);
					}
					break;
				}
				filecount++;
				if (emptydir)
				{
					swprintf_s(filelist[i], L"%s\\%s", dirname, &filenames.GetBuffer()[prevpos + 1]);
					swprintf_s(filename[i], L"%s/%s", dirupload, &filenames.GetBuffer()[prevpos + 1]);
				}
				else
				{
					swprintf_s(filelist[i], L"%s\\%s", dirname, &filenames.GetBuffer()[prevpos + 1]);
					swprintf_s(filename[i], L"%s", &filenames.GetBuffer()[prevpos + 1]);
				}
					
			}

			ZeroMemory(returned, sizeof(returned));
			for (int i = 0; i < filecount; i++)
			{
				file.open(filelist[i],ios::in | ios::binary);
				if (file.is_open())
				{
					ULONG length=0;
					
					fileopened[i] = true;
					file.seekg(0, file.end);
					length = file.tellg();
					file.seekg(0, file.beg);
					if (length >= 102400000)
					{
						filesize[i] = false;
						continue;
					}
					filesize[i] = true;
					vector<std::byte> buffer(length);
					char filenamex[MAX_FILES_COUNT][MAX_PATH] = { 0 };
					wcstombs(filenamex[i], filename[i], sizeof(filenamex[i]));
					file.read(reinterpret_cast<char*>(buffer.data()), length);
					if (SteamRemoteStorage()->FileWrite(filenamex[i], reinterpret_cast<char*>(buffer.data()), length) == false)
					{
						MessageBox(L"Error uploading file!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
					}
					file.close();
					Sleep(300);
				}
			}
			
	
			//if (buffer != nullptr) free(buffer);
		}
	}
	Clearlist();
	GetFiles();
	Sleep(1);
}

void OnSteamCallComplete(RemoteStorageFileWriteAsyncComplete_t _callback, bool _failure)
{

}
void CsteamcloudDlg::OnBnClickedDownload()
{
	bool fileread = false;
	bool filewritten = false;
	bool overwrite = false;
	bool openfile = false;
	wchar_t szFilter[] = L"All Files (*.*)|*.*||";
	wchar_t filename[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	wchar_t filelist[MAX_FILES_COUNT][MAX_PATH] = { 0 };
	HANDLE testfile = NULL;
	fstream file;
	wchar_t buffer[MAX_PATH] = { 0 };
	POSITION pos = 0;
	DWORD size = 102400000;
	CFileDialog filedialog(FALSE, NULL, NULL, OFN_EXPLORER | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST, szFilter, this,sizeof(OPENFILENAMEW));
	CFolderPickerDialog dialog((LPCTSTR)NULL, OFN_EXPLORER | OFN_NONETWORKBUTTON | OFN_PATHMUSTEXIST | OFN_CREATEPROMPT, this, NULL, FALSE);
	//CFolderPickerDialog(NULL, );
	int files[10] = { -1 };
	fill_n(files, 10, -1);
	int count = 0;
	int mark = -1;
	pos = listfiles->GetFirstSelectedItemPosition();
	if (pos == NULL)
	{
		MessageBox(L"No file is selected! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		goto end;
	}
	mark = listfiles->GetNextSelectedItem(pos);
	while (mark != -1)
	{
		if (count == 10) break; // break if we've reached the maximum number of selected items
		files[count] = mark;
		wcscpy_s(filename[count], listfiles->GetItemText(mark, 0));// save the selected item index
		count++; // increment the count
		mark = listfiles->GetNextSelectedItem(pos);
		
	}
	if (count == 1)
	{
		wchar_t bufx[MAX_PATH] = { 0 };
		wcscpy_s(bufx, filename[0]);
		filedialog.GetOFN().lpstrFile = bufx;
		if (filedialog.DoModal() != IDOK) goto end;
		
	}
	else
	{
		if (dialog.DoModal() != IDOK) goto end;
	}
	openfile = true;
	for (int i = 0; i < count; i++)
	{
		if (count == 1)
		{
			overwrite = false;
			openfile = false;
			swprintf_s(filelist[i], L"%s\\%s", filedialog.GetFolderPath().GetString(), filedialog.GetFileName().GetString());
			testfile = CreateFile(filelist[i], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (testfile != INVALID_HANDLE_VALUE)
			{
				openfile = true;
			}
			CloseHandle(testfile);
			if (openfile)
			{
				if (MessageBox(L"file exist. Do you want to overwrite them?", L"ERROR", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) == IDYES)
				{
					overwrite = true;
					BOOL res = DeleteFileW(filelist[i]);
					if (res == 0)
					{
						DWORD err = GetLastError();
						MessageBox(L"Failed to delete the file! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
						goto end;
					}
				}
			}
			testfile = CreateFileW(filelist[i], GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (testfile == INVALID_HANDLE_VALUE)
			{
				DWORD error = GetLastError();
				filewritten = false;
				MessageBox(L"Failed to create the file! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
				goto end;
			}
			CloseHandle(testfile);
			file.open(filelist[i], ios::out | ios::binary);
			if (file.is_open())
			{
				CT2A text(filename[i]);
				size = (size_t)SteamRemoteStorage()->GetFileSize(text.m_psz);
				vector<std::byte> buffer(size);
				if (SteamRemoteStorage()->FileRead(text.m_psz, reinterpret_cast<char*>(buffer.data()), (int32)buffer.size()) == 0)
				{
					MessageBox(L"Failed to read the file/file not exist in cloud! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
					file.close();
				}
				file.write(reinterpret_cast<char*>(buffer.data()), buffer.size());
				file.close();
			}
			else
			{
				MessageBox(L"Failed to open the file! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
				goto end;
			}
		}
		else
		{
			swprintf_s(filelist[i], L"%s\\%s", dialog.GetFolderPath().GetString(), filename[i]);
			testfile = CreateFile(filelist[i], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (testfile != INVALID_HANDLE_VALUE)
			{
				openfile = false;
			}
			CloseHandle(testfile);
		}
	}
	overwrite = true;
	if (!openfile)
	{
		if (MessageBox(L"One or more files exist. Do you want to overwrite them?", L"ERROR", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) == IDYES)
		{
			overwrite = true;
			for (int i = 0; i < count; i++)
			{
				testfile = CreateFileW(filelist[i], GENERIC_READ, NULL, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (testfile != INVALID_HANDLE_VALUE)
				{
					CloseHandle(testfile);
					BOOL res = DeleteFileW(filelist[i]);
					if (res == 0)
					{
						DWORD err = GetLastError();
						MessageBox(L"Failed to delete the file! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
						goto end;
					}
				}
			}
		}
		else
		{
			overwrite = false;
		}
	}
	if (overwrite)
	{ 
		filewritten = true;
		fileread = true;
		for (int i = 0; i < count; i++)
		{
			
			testfile = CreateFileW(filelist[i], GENERIC_WRITE, NULL, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (testfile == INVALID_HANDLE_VALUE)
			{
				DWORD error = GetLastError();
				filewritten = false;
				continue;
			}
			CloseHandle(testfile);
			file.open(filelist[i], ios::out | ios::binary);
			if (file.is_open())
			{
				CT2A text(filename[i]);
				size = (size_t)SteamRemoteStorage()->GetFileSize(text.m_psz);
				vector<std::byte> buffer(size);
				int32 size2 = (int32)buffer.size();
				if (SteamRemoteStorage()->FileRead(text.m_psz, reinterpret_cast<char*>(buffer.data()), size2) == 0)
				{
					fileread = false;
					file.close();
					continue;
				}
				file.write(reinterpret_cast<char*>(buffer.data()), buffer.size());
				file.close();
			}
			else
			{
				filewritten = false;
			}
		}
		if (!filewritten)
		{
			MessageBox(L"One or more files failed to save! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		}
		if (!fileread)
		{
			MessageBox(L"Failed to read some file/s from cloud/file/s not exist in cloud! Please try again!", L"ERROR", MB_OK | MB_ICONERROR | MB_TOPMOST);
		}
	}	
end:
	Sleep(1);
}

void CsteamcloudDlg::OnBnClickedRefresh()
{
	Clearlist();
	GetFiles();
}


void CsteamcloudDlg::OnBnClickedDisconnect()
{
	SetEnvironmentVariableA("SteamAppID", NULL);
	//steam.Init();
	SteamAPI_Shutdown();
	//steam.Clear();
	init = false;
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
	Bytes->SetCheck(BST_CHECKED);
	Kbytes->SetCheck(BST_UNCHECKED);
	Mbytes->SetCheck(BST_UNCHECKED);
	LVCOLUMN col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT;
	listfiles->GetColumn(2, &col);
	col.pszText = L"Size (B)";
	listfiles->SetColumn(2, &col);
	Clearlist();
	GetFiles();
}


void CsteamcloudDlg::OnBnClickedKbytes()
{
	sizeunit = 1;
	Bytes->SetCheck(BST_UNCHECKED);
	Kbytes->SetCheck(BST_CHECKED);
	Mbytes->SetCheck(BST_UNCHECKED);
	LVCOLUMN col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT;
	listfiles->GetColumn(2, &col);
	col.pszText = L"Size (KB)";
	listfiles->SetColumn(2, &col);
	Clearlist();
	GetFiles();
}


void CsteamcloudDlg::OnBnClickedMbytes()
{
	sizeunit = 2;
	Bytes->SetCheck(BST_UNCHECKED);
	Kbytes->SetCheck(BST_UNCHECKED);
	Mbytes->SetCheck(BST_CHECKED);
	LVCOLUMN col;
	memset(&col, 0, sizeof(col));
	col.mask = LVCF_TEXT;
	listfiles->GetColumn(2, &col);
	col.pszText = L"Size (MB)";
	listfiles->SetColumn(2, &col);
	Clearlist();
	GetFiles();
}
