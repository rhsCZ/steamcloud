#include "pch.h"
#include "framework.h"
#include "steamcloud.h"
#include "steamcloudDlg.h"
#include <tchar.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CsteamcloudApp

BEGIN_MESSAGE_MAP(CsteamcloudApp, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()

CsteamcloudApp::CsteamcloudApp()
{
	
}
CsteamcloudApp::~CsteamcloudApp()
{

}
CsteamcloudApp theApp;
BOOL CsteamcloudApp::InitInstance()
{
	AfxInitRichEdit2();
	//AfxTermLocalData(NULL, TRUE);
	//AfxTlsRelease();
	//CWinApp::InitInstance();
	//SetErrorMode(0); //only debug
	/*if (LoadLibraryA("libssl-3.dll") == NULL) //not important for crypto
	{
		errormes = GetLastError();
		error++;
	}
	/*if (LoadLibraryA("libcrypto-3.dll") == NULL) //
	{
		//errormes = GetLastError(); //for debug
		error++;
	}
	if (error > 0)
	{
		exit(-5);
	}*/
	//SetDlgItemTextW(IDC_COMBO1, L"MD5");
	
	//SetRegistryKey(_T("Local AppWizard-Generated Applications")); //not needed

/*	CsteamcloudApp::m_pMainWnd->ShowWindow(SW_SHOW);
	if (m_pDialog != NULL)
	{
		BOOL ret = m_pDialog->Create(IDD_HASHER2_DIALOG);

		if (!ret)   //Create failed.
		{
			AfxMessageBox(_T("Error creating Dialog"));
			delete m_pDialog;
		}
		else {
			m_pDialog->ShowWindow(SW_SHOW);
		}
		
	}
	*/
#ifdef _DEBUG
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
	_CrtDumpMemoryLeaks();
#endif
	CsteamcloudDlg dlg;
	m_pMainWnd = &dlg;
	//DWORD resultxy;
	INT_PTR nResponse = dlg.DoModal();
	//resultxy = GetLastError();
	//if (dlg.Create(IDD_HASHER2_DIALOG, 0) == 0)
	//{
		//exit(-1);
	//}
	if (nResponse == IDOK)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with OK
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with Cancel
	}
	else if (nResponse == -1)
	{
		TRACE(traceAppMsg, 0, "Warning: dialog creation failed, so application is terminating unexpectedly.\n");
	}
	else if (nResponse == IDABORT)
	{
		TRACE(traceAppMsg, 0, "Warning: dialog creation failed, so application is terminating unexpectedly2.\n");
	}


#if !defined(_AFXDLL) && !defined(_AFX_NO_MFC_CONTROLS_IN_DIALOGS)
	ControlBarCleanUp();
#endif

	return FALSE;
}
int CsteamcloudApp::ExitInstance()
{

	CsteamcloudDlg abc;
	Shell_NotifyIcon(NIM_DELETE, &abc.m_nidIconData);
/*#ifdef _WIN64
	FreeLibrary(GetModuleHandleW(L"steam-api64.dll"));
#else
	FreeLibrary(GetModuleHandleW(L"steam_api.dll"));
#endif
*/
	
#ifdef _DEBUG
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
	_CrtDumpMemoryLeaks();
	
#endif
	return 0;
}
