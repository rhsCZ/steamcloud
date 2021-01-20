
// hasher2.h : main header file for the PROJECT_NAME application
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'pch.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols



// Csteamupload2App:
// See hasher2.cpp for the implementation of this class
//

class CsteamuploadApp : public CWinApp
{
public:
	CsteamuploadApp();

// Overrides
public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	int error = 0;
	DWORD errormes = 0;
	CDialog* m_pDialog = new CDialog;
// Implementation

	DECLARE_MESSAGE_MAP()
};

extern CsteamuploadApp theApp;