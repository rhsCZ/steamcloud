#pragma once


// prompt dialog

class prompt : public CDialog
{
	DECLARE_DYNAMIC(prompt)

public:
	prompt(CWnd* pParent = nullptr);   // standard constructor
	virtual ~prompt();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG1 };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedOk();
};
