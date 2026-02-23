#if !defined(AFX_ADVANCEDSETTINGS_H__08B61D97_80EA_4C27_87D7_C0276A2932B8__INCLUDED_)
#define AFX_ADVANCEDSETTINGS_H__08B61D97_80EA_4C27_87D7_C0276A2932B8__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// AdvancedSettings.h : header file — Phase 5: Podcast Settings tab
//

/////////////////////////////////////////////////////////////////////////////
// CAdvancedSettings dialog

class CAdvancedSettings : public CResizableDialog
{
// Construction
public:
	CAdvancedSettings(CWnd* pParent = NULL);   // standard constructor

    void EnableDisable();
    void EnableDisablePodcast();
    void PopulatePodcastFromYP();

// Dialog Data
	//{{AFX_DATA(CAdvancedSettings)
	enum { IDD = IDD_PROPPAGE_LARGE2 };

	// Archive / recording
	CEdit	m_ArchiveDirectoryCtrl;
	CButton	m_SavewavCtrl;
	CString	m_ArchiveDirectory;
	BOOL	m_Savestream;
	BOOL	m_Savewav;

	// Logging
	CString	m_Logfile;
	CString	m_Loglevel;

	// Podcast RSS
	BOOL    m_GenerateRSS;
	BOOL    m_RSSUseYP;
	CString m_PodcastTitle;
	CString m_PodcastAuthor;
	CString m_PodcastCategory;
	CString m_PodcastLanguage;
	CString m_PodcastCopyright;
	CString m_PodcastWebsite;
	CString m_PodcastCoverArt;
	CString m_PodcastDescription;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAdvancedSettings)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CAdvancedSettings)
	virtual BOOL OnInitDialog();
	afx_msg void OnSavestream();
	afx_msg void OnGenerateRSS();
	afx_msg void OnRSSUseYP();
	afx_msg void OnBrowseArchive();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_ADVANCEDSETTINGS_H__08B61D97_80EA_4C27_87D7_C0276A2932B8__INCLUDED_)
