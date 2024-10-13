
// hasher2Dlg.h : header file
//
#pragma once
#include "steam/steam_api.h"
#include "afxdialogex.h"
#include "afxwin.h"
#include <vector>
#include <iostream>
#include "functions.h"
#include <iterator>
#include <algorithm>
//#include <thread>
//#include <fstream>
using namespace std;
#define MAX_UNICODE_PATH 32766
#define bufferSize 10
#define WM_SHOWPAGE WM_APP+2
#define WM_TRAY_ICON_NOTIFY_MESSAGE (WM_USER + 1)
#define MAX_FILES_COUNT (ULONG)10
#define MAX_FILES_BUFFER_SIZE (MAX_FILES_COUNT*MAX_UNICODE_PATH)-1




// Chasher2Dlg dialog
class CsteamcloudDlg : public CDialog
{
private:
	BOOL m_bMinimizeToTray;
	BOOL			m_bTrayIconVisible;
	CMenu			m_mnuTrayMenu;
	UINT			m_nDefaultMenuItem;
	afx_msg LRESULT OnTrayNotify(WPARAM wParam, LPARAM lParam);
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
	virtual void OnTrayMouseMove(CPoint pt);	// standard constructor

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_STEAMCLOUD_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support

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
	bool trayenable;
	bool minimizeen;
	bool init = false;
	int sizeunit = 0;
	SteamAPICallCompleted_t steamcall;
	CButton* trayen = {};
	CButton* Bytes = {};
	CButton* Kbytes = {};
	CButton* Mbytes = {};
	CButton* checkbox = {};
	CButton* upload = {};
	CButton* download = {};
	CButton* connect = {};
	CButton* exitb = {};
	CButton* cancel = {};
	CButton* uploaddir = {};
	CButton* deletefile = {};
	CButton* refresh = {};
	CButton* disconnect = {};
	CStatic* quota = {};
	//CRect buttonrect = {};
	//CRgn buttonrgn = {};
	//CSteamAPIContext steam;
	//HSteamPipe steampipe;
	//HSteamUser steamuser;
	//ISteamClient* steam;
	ISteamRemoteStorage* steamremote;
	HKEY traykey;
	CListCtrl* listfiles;
	DWORD traykeyvalue;
	//LSTATUS error;
	unsigned long type;
	DWORD keycreate;
	afx_msg void OnOpen();
	afx_msg void OnMinimize();
	afx_msg void OnBnClickedMinEn();
	afx_msg void OnBnClickedTrayEn();
	PCHAR* CommandLineToArgvA(PCHAR CmdLine, int* _argc);
	//bool ExtractResource(uint16_t ResourceID, char* OutputFileName, char* path, const char* ResType);
	afx_msg void OnBnClickedConnect();
	afx_msg void OnBnClickedDelete();
	afx_msg void OnBnClickedUpload();
	afx_msg void OnBnClickedDirupload();
	afx_msg void OnBnClickedDownload();
	void GetFiles();
	void Clearlist();
	//void OnSteamCallComplete(RemoteStorageFileWriteAsyncComplete_t _callback, bool _failure);
	afx_msg void OnBnClickedRefresh();
	afx_msg void OnBnClickedDisconnect();
	afx_msg void OnBnClickedBytes();
	afx_msg void OnBnClickedKbytes();
	afx_msg void OnBnClickedMbytes();
};
