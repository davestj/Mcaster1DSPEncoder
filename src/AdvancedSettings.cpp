// AdvancedSettings.cpp : implementation file — Phase 5: Podcast Settings tab
//

#include "stdafx.h"
#include "mcaster1dspencoder.h"
#include "AdvancedSettings.h"
#include <shlobj.h>     // SHBrowseForFolder, SHGetPathFromIDList

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CAdvancedSettings dialog

CAdvancedSettings::CAdvancedSettings(CWnd* pParent /*=NULL*/)
	: CResizableDialog(CAdvancedSettings::IDD, pParent)
{
	//{{AFX_DATA_INIT(CAdvancedSettings)
	m_ArchiveDirectory = _T("");
	m_Logfile = _T("");
	m_Loglevel = _T("");
	m_Savestream = FALSE;
	m_Savewav = FALSE;
	m_GenerateRSS = FALSE;
	m_RSSUseYP = FALSE;
	m_PodcastTitle = _T("");
	m_PodcastAuthor = _T("");
	m_PodcastCategory = _T("");
	m_PodcastLanguage = _T("en-us");
	m_PodcastCopyright = _T("");
	m_PodcastWebsite = _T("");
	m_PodcastCoverArt = _T("");
	m_PodcastDescription = _T("");
	//}}AFX_DATA_INIT
}


void CAdvancedSettings::DoDataExchange(CDataExchange* pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAdvancedSettings)
	// Archive / recording
	DDX_Control(pDX, IDC_ARCHIVE_DIRECTORY, m_ArchiveDirectoryCtrl);
	DDX_Control(pDX, IDC_SAVEWAV, m_SavewavCtrl);
	DDX_Text(pDX, IDC_ARCHIVE_DIRECTORY, m_ArchiveDirectory);
	DDX_Check(pDX, IDC_SAVESTREAM, m_Savestream);
	DDX_Check(pDX, IDC_SAVEWAV, m_Savewav);
	// Logging
	DDX_Text(pDX, IDC_LOGFILE, m_Logfile);
	DDX_Text(pDX, IDC_LOGLEVEL, m_Loglevel);
	// Podcast RSS
	DDX_Check(pDX, IDC_GENERATE_RSS, m_GenerateRSS);
	DDX_Check(pDX, IDC_RSS_USE_YP, m_RSSUseYP);
	DDX_Text(pDX, IDC_PODCAST_TITLE, m_PodcastTitle);
	DDX_Text(pDX, IDC_PODCAST_AUTHOR, m_PodcastAuthor);
	DDX_Text(pDX, IDC_PODCAST_CATEGORY, m_PodcastCategory);
	DDX_Text(pDX, IDC_PODCAST_LANGUAGE, m_PodcastLanguage);
	DDX_Text(pDX, IDC_PODCAST_COPYRIGHT, m_PodcastCopyright);
	DDX_Text(pDX, IDC_PODCAST_WEBSITE, m_PodcastWebsite);
	DDX_Text(pDX, IDC_PODCAST_COVERART, m_PodcastCoverArt);
	DDX_Text(pDX, IDC_PODCAST_DESCRIPTION, m_PodcastDescription);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CAdvancedSettings, CResizableDialog)
	//{{AFX_MSG_MAP(CAdvancedSettings)
	ON_BN_CLICKED(IDC_SAVESTREAM, OnSavestream)
	ON_BN_CLICKED(IDC_GENERATE_RSS, OnGenerateRSS)
	ON_BN_CLICKED(IDC_RSS_USE_YP, OnRSSUseYP)
	ON_BN_CLICKED(IDC_BROWSE_ARCHIVE, OnBrowseArchive)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CAdvancedSettings message handlers

void CAdvancedSettings::EnableDisable()
{
    if (m_Savestream) {
        m_ArchiveDirectoryCtrl.EnableWindow(TRUE);
        m_SavewavCtrl.EnableWindow(TRUE);
        GetDlgItem(IDC_BROWSE_ARCHIVE)->EnableWindow(TRUE);
        GetDlgItem(IDC_GENERATE_RSS)->EnableWindow(TRUE);
    }
    else {
        m_ArchiveDirectoryCtrl.EnableWindow(FALSE);
        m_SavewavCtrl.EnableWindow(FALSE);
        GetDlgItem(IDC_BROWSE_ARCHIVE)->EnableWindow(FALSE);
        GetDlgItem(IDC_GENERATE_RSS)->EnableWindow(FALSE);
    }
    EnableDisablePodcast();
}

void CAdvancedSettings::EnableDisablePodcast()
{
    BOOL podcastActive = m_Savestream && m_GenerateRSS;
    GetDlgItem(IDC_RSS_USE_YP)->EnableWindow(podcastActive);
    GetDlgItem(IDC_PODCAST_TITLE)->EnableWindow(podcastActive);
    GetDlgItem(IDC_PODCAST_AUTHOR)->EnableWindow(podcastActive);
    GetDlgItem(IDC_PODCAST_CATEGORY)->EnableWindow(podcastActive);
    GetDlgItem(IDC_PODCAST_LANGUAGE)->EnableWindow(podcastActive);
    GetDlgItem(IDC_PODCAST_COPYRIGHT)->EnableWindow(podcastActive);
    GetDlgItem(IDC_PODCAST_WEBSITE)->EnableWindow(podcastActive);
    GetDlgItem(IDC_PODCAST_COVERART)->EnableWindow(podcastActive);
    GetDlgItem(IDC_PODCAST_DESCRIPTION)->EnableWindow(podcastActive);
}

void CAdvancedSettings::PopulatePodcastFromYP()
{
    // Reach up two levels: tab page parent = tab ctrl, tab ctrl parent = CConfig
    CWnd* pConfig = GetParent() ? GetParent()->GetParent() : NULL;
    if (pConfig) {
        // Signal CConfig to push YP values into our podcast edit controls
        pConfig->SendMessage(WM_USER + 101, 0, (LPARAM)this);
    }
}

BOOL CAdvancedSettings::OnInitDialog()
{
	CResizableDialog::OnInitDialog();

	// ResizableLib anchors
	AddAnchor(IDC_PODCAST_GROUP,       TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_ARCHIVE_DIRECTORY,   TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_BROWSE_ARCHIVE,      TOP_RIGHT, TOP_RIGHT);
	AddAnchor(IDC_PODCAST_TITLE,       TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_PODCAST_AUTHOR,      TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_PODCAST_CATEGORY,    TOP_LEFT, TOP_CENTER);
	AddAnchor(IDC_PODCAST_COPYRIGHT,   TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_PODCAST_WEBSITE,     TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_PODCAST_COVERART,    TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_PODCAST_DESCRIPTION, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_LOG_GROUP,           BOTTOM_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_LOGFILE,             BOTTOM_LEFT, BOTTOM_RIGHT);
	AddAllOtherAnchors(TOP_LEFT);

	EnableDisable();

	return TRUE;
}

void CAdvancedSettings::OnSavestream()
{
    UpdateData(TRUE);
    EnableDisable();
}

void CAdvancedSettings::OnGenerateRSS()
{
    UpdateData(TRUE);
    EnableDisablePodcast();
}

void CAdvancedSettings::OnRSSUseYP()
{
    UpdateData(TRUE);
    if (m_RSSUseYP) {
        PopulatePodcastFromYP();
    }
}

void CAdvancedSettings::OnBrowseArchive()
{
    BROWSEINFO bi = { 0 };
    bi.hwndOwner = m_hWnd;
    bi.lpszTitle = _T("Select Archive Directory");
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
    if (pidl) {
        TCHAR szPath[MAX_PATH];
        if (SHGetPathFromIDList(pidl, szPath)) {
            m_ArchiveDirectory = szPath;
            UpdateData(FALSE);
        }
        CoTaskMemFree(pidl);
    }
}
