// prompt.cpp : implementation file
//

#include "pch.h"
#include "steamcloud.h"
#include "steamcloudDlg.h"
#include "prompt.h"
#include "afxdialogex.h"


// prompt dialog

IMPLEMENT_DYNAMIC(prompt, CDialog)

prompt::prompt(CWnd* pParent /*=nullptr*/)
	: CDialog(IDD_DIALOG1, pParent)
{

}

prompt::~prompt()
{
}

void prompt::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(prompt, CDialog)
	ON_BN_CLICKED(IDOK, &prompt::OnBnClickedOk)
END_MESSAGE_MAP()


// prompt message handlers


void prompt::OnBnClickedOk()
{
	CString texxt;
	CsteamcloudDlg x;
	GetDlgItemText(IDC_RICHEDIT21, texxt);
	CT2W text(texxt);
	wcscpy(x.returned, text.m_psz);
	CDialog::OnOK();
}
