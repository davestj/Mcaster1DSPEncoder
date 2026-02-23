#if !defined(AFX_CONFIG_H__45134EFE_9919_4427_8C2A_A264B88E6AA5__INCLUDED_)
#define AFX_CONFIG_H__45134EFE_9919_4427_8C2A_A264B88E6AA5__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// Config.h : header file
//
#include "BasicSettings.h"
#include "YPSettings.h"
#include "AdvancedSettings.h"
#include "ICY22Settings.h"
#include "libmcaster1dspencoder.h"

// WM_SET_PODCAST_FROM_YP — sent by AdvancedSettings to CConfig when user
// checks "Populate RSS from YP Settings". CConfig copies YP fields into
// the podcast edit controls and calls UpdateData(FALSE) on advSettings.
#define WM_SET_PODCAST_FROM_YP  (WM_USER + 101)

/////////////////////////////////////////////////////////////////////////////
// CConfig dialog

class CConfig : public CResizableDialog
{
// Construction
public:
	CConfig(CWnd* pParent = NULL);   // standard constructor
    ~CConfig();

    void GlobalsToDialog(mcaster1Globals *g);
    void DialogToGlobals(mcaster1Globals *g);
// Dialog Data
	//{{AFX_DATA(CConfig)
	enum { IDD = IDD_CONFIG };
	CTabCtrl	m_TabCtrl;
	//}}AFX_DATA


    CBasicSettings    *basicSettings;
    CYPSettings       *ypSettings;
    CAdvancedSettings *advSettings;
    CICY22Settings    *icy22Settings;
    CDialog *parentDialog;
    int     currentEnc;

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CConfig)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CConfig)
	virtual BOOL OnInitDialog();
	afx_msg void OnSelchangeTab1(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnClose();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg LRESULT OnSetPodcastFromYP(WPARAM wParam, LPARAM lParam);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_CONFIG_H__45134EFE_9919_4427_8C2A_A264B88E6AA5__INCLUDED_)
