// Config.cpp : implementation file
//

#include "stdafx.h"
#include "mcaster1dspencoder.h"
#include "Config.h"
#include "MainWindow.h"
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CConfig dialog


CConfig::CConfig(CWnd* pParent /*=NULL*/)
	: CResizableDialog(CConfig::IDD, pParent)
{
	//{{AFX_DATA_INIT(CConfig)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT

    basicSettings = new CBasicSettings();
    ypSettings = new CYPSettings();
    advSettings = new CAdvancedSettings();
    currentEnc = 0;
}

CConfig::~CConfig()
{
    if (basicSettings) {
        delete basicSettings;
    }
    if (ypSettings) {
        delete ypSettings;
    }
    if (advSettings) {
        delete advSettings;
    }
}
void CConfig::DoDataExchange(CDataExchange* pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CConfig)
	DDX_Control(pDX, IDC_TAB1, m_TabCtrl);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CConfig, CResizableDialog)
	//{{AFX_MSG_MAP(CConfig)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB1, OnSelchangeTab1)
	ON_WM_CLOSE()
	ON_WM_SIZE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CConfig message handlers

BOOL CConfig::OnInitDialog()
{
	CResizableDialog::OnInitDialog();

	m_TabCtrl.InsertItem(0, _T("Basic Settings"));
	m_TabCtrl.InsertItem(1, _T("YP Settings"));
	m_TabCtrl.InsertItem(2, _T("Advanced Settings"));

    basicSettings->Create((UINT)IDD_PROPPAGE_LARGE, this);
    ypSettings->Create((UINT)IDD_PROPPAGE_LARGE1, this);
    advSettings->Create((UINT)IDD_PROPPAGE_LARGE2, this);

    basicSettings->ShowWindow(SW_SHOW);
    ypSettings->ShowWindow(SW_HIDE);
    advSettings->ShowWindow(SW_HIDE);

	// ResizableLib anchors
	AddAnchor(IDC_TAB1,  TOP_LEFT,    BOTTOM_RIGHT);
	AddAnchor(IDOK,      BOTTOM_LEFT, BOTTOM_LEFT);
	AddAnchor(IDCANCEL,  BOTTOM_RIGHT, BOTTOM_RIGHT);

	// Prevent dialog from shrinking below its initial template size so that
	// combo boxes inside child tab pages never get a negative width
	CRect rcInit;
	GetWindowRect(&rcInit);
	SetMinTrackSize(CSize(rcInit.Width(), rcInit.Height()));

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CConfig::OnSize(UINT nType, int cx, int cy)
{
	if (nType == SIZE_MINIMIZED) {
		CResizableDialog::OnSize(nType, cx, cy);
		return;
	}

	/* Suppress all intermediate repaints during the cascade of ResizableLib
	   anchor moves and child-dialog MoveWindow calls.  Without this, each
	   combo box receives its own WM_ERASEBKGND + WM_PAINT in sequence,
	   causing visible flicker and momentary disappearance of controls. */
	LockWindowUpdate();

	CResizableDialog::OnSize(nType, cx, cy);

	if (IsWindow(m_hWnd) && IsWindow(m_TabCtrl.m_hWnd)) {
		CRect rcTab;
		m_TabCtrl.GetWindowRect(&rcTab);
		ScreenToClient(&rcTab);
		m_TabCtrl.AdjustRect(FALSE, &rcTab);

		/* bRepaint=FALSE: defer the repaint to our single RedrawWindow below. */
		if (basicSettings && IsWindow(basicSettings->m_hWnd))
			basicSettings->MoveWindow(&rcTab, FALSE);
		if (ypSettings && IsWindow(ypSettings->m_hWnd))
			ypSettings->MoveWindow(&rcTab, FALSE);
		if (advSettings && IsWindow(advSettings->m_hWnd))
			advSettings->MoveWindow(&rcTab, FALSE);
	}

	UnlockWindowUpdate();
	/* One clean repaint of the entire dialog tree — no flicker. */
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void CConfig::OnSelchangeTab1(NMHDR* pNMHDR, LRESULT* pResult) 
{
	// TODO: Add your control notification handler code here
	int selected = m_TabCtrl.GetCurSel();
    if (selected == 0) {
        basicSettings->ShowWindow(SW_SHOW);
        ypSettings->ShowWindow(SW_HIDE);
        advSettings->ShowWindow(SW_HIDE);
    }
    if (selected == 1) {
        ypSettings->ShowWindow(SW_SHOW);
        basicSettings->ShowWindow(SW_HIDE);
        advSettings->ShowWindow(SW_HIDE);
    }
    if (selected == 2) {
        basicSettings->ShowWindow(SW_HIDE);
        ypSettings->ShowWindow(SW_HIDE);
        advSettings->ShowWindow(SW_SHOW);
    }

	*pResult = 0;
}
void CConfig::GlobalsToDialog(mcaster1Globals *g) {
    char    buf[255] = "";
    /*Basic Settings 
	CString	m_Bitrate;
	CString	m_Channels;
	CString	m_EncoderType;
	CString	m_Mountpoint;
	CString	m_Password;
	CString	m_Quality;
	CString	m_ReconnectSecs;
	CString	m_Samplerate;
	CString	m_ServerIP;
	CString	m_ServerPort;
	CString	m_ServerType;
    */
    currentEnc = g->encoderNumber;

    sprintf(buf, "%d", getCurrentBitrate(g));
    basicSettings->m_Bitrate = buf;
    sprintf(buf, "%d", getCurrentChannels(g));
    basicSettings->m_Channels = buf;
    sprintf(buf, "%d", getCurrentSamplerate(g));
    basicSettings->m_Samplerate = buf;

	if (g->gOggBitQualFlag == 0) { // Quality
		basicSettings->m_UseBitrate = false;
	}
	else {
		basicSettings->m_UseBitrate = true;
	}

    /* Determine encoder type string for the UI combo box */
    if (g->gOpusFlag) {
        basicSettings->m_EncoderType = "Opus";
        basicSettings->m_Quality = "";
    } else if (g->gAACFlag && g->fdkAacProfile == 29) {
        basicSettings->m_EncoderType = "AAC++";
        basicSettings->m_Quality = g->gAACQuality;
    } else if (g->gAACFlag && g->fdkAacProfile == 5) {
        basicSettings->m_EncoderType = "AAC+";
        basicSettings->m_Quality = g->gAACQuality;
    } else if (g->gAACFlag && g->fdkAacProfile == 2) {
        basicSettings->m_EncoderType = "AAC-LC";
        basicSettings->m_Quality = g->gAACQuality;
    } else if (g->gAACPFlag) {
        basicSettings->m_EncoderType = "AAC+";   /* legacy AAC Plus */
        basicSettings->m_Quality = "";
    } else if (g->gAACFlag) {
        basicSettings->m_EncoderType = "AAC-LC"; /* legacy AAC */
        basicSettings->m_Quality = g->gAACQuality;
    } else if (g->gLAMEFlag) {
        basicSettings->m_EncoderType = "MP3 -- LAME (built-in)";
        basicSettings->m_Quality = g->gOggQuality;
    } else if (g->gOggFlag) {
        basicSettings->m_EncoderType = "OggVorbis";
        basicSettings->m_Quality = g->gOggQuality;
    } else if (g->gFLACFlag) {
        basicSettings->m_EncoderType = "Ogg FLAC";
        basicSettings->m_Quality = g->gOggQuality;
    }
    basicSettings->m_EncoderTypeCtrl.SelectString(0, basicSettings->m_EncoderType);
    basicSettings->m_Mountpoint = g->gMountpoint;
    basicSettings->m_Password = g->gPassword;
    sprintf(buf, "%d", g->gReconnectSec);

    basicSettings->m_ReconnectSecs = buf;
    basicSettings->m_ServerIP = g->gServer;
    basicSettings->m_ServerPort = g->gPort;

    if (g->gShoutcastFlag) {
        basicSettings->m_ServerType = "Shoutcast";
    }
    if (g->gIcecast2Flag) {
        basicSettings->m_ServerType = "Icecast2";
    }
    // Mcaster1DNAS is Icecast2-compatible; restore exact string if saved
    if (strcmp(g->gServerType, "Mcaster1DNAS") == 0) {
        basicSettings->m_ServerType = "Mcaster1DNAS";
    }

    basicSettings->m_ServerTypeCtrl.SelectString(0, basicSettings->m_ServerType);

	if (g->LAMEJointStereoFlag) {
		basicSettings->m_JointStereo = true;
	}
    basicSettings->UpdateData(FALSE);
    basicSettings->UpdateFields();
    /* YP Settings
    	BOOL	m_Public;
	CString	m_StreamDesc;
	CString	m_StreamGenre;
	CString	m_StreamName;
	CString	m_StreamURL;
    */
    if (g->gPubServ) {
        ypSettings->m_Public = true;
    }
    else {
        ypSettings->m_Public = false;
    }
    ypSettings->m_StreamDesc = g->gServDesc;
    ypSettings->m_StreamName = g->gServName;
    ypSettings->m_StreamGenre = g->gServGenre;
    ypSettings->m_StreamURL = g->gServURL;
    ypSettings->m_StreamAIM = g->gServAIM;
    ypSettings->m_StreamICQ = g->gServICQ;
    ypSettings->m_StreamIRC = g->gServIRC;
    ypSettings->UpdateData(FALSE);
    ypSettings->EnableDisable();
    /* Advanced Settings
    	CString	m_ArchiveDirectory;
	CString	m_Logfile;
	CString	m_Loglevel;
	BOOL	m_Savestream;
	BOOL	m_Savewav;
    */
    advSettings->m_Savestream = g->gSaveDirectoryFlag;
    advSettings->m_ArchiveDirectory = g->gSaveDirectory;
    sprintf(buf, "%d", g->gLogLevel);
    advSettings->m_Loglevel = buf;
	advSettings->m_Logfile = g->gLogFile;

    advSettings->m_Savewav = g->gSaveAsWAV;
    advSettings->UpdateData(FALSE);
    advSettings->EnableDisable();
}

void CConfig::DialogToGlobals(mcaster1Globals *g) {
    char    buf[255] = "";
    /*Basic Settings 
	CString	m_Bitrate;
	CString	m_Channels;
	CString	m_EncoderType;
	CString	m_Mountpoint;
	CString	m_Password;
	CString	m_Quality;
	CString	m_ReconnectSecs;
	CString	m_Samplerate;
	CString	m_ServerIP;
	CString	m_ServerPort;
	CString	m_ServerType;
    */
    currentEnc = g->encoderNumber;

//    basicSettings->UpdateData(TRUE);

    g->currentBitrate = atoi(LPCSTR(basicSettings->m_Bitrate));
    g->currentChannels = atoi(LPCSTR(basicSettings->m_Channels));
    g->currentSamplerate = atoi(LPCSTR(basicSettings->m_Samplerate));

    /* Clear all codec flags, then set exactly one based on the UI selection */
    g->gOpusFlag  = 0;
    g->gAACFlag   = 0;
    g->gAACPFlag  = 0;
    g->gLAMEFlag  = 0;
    g->gOggFlag   = 0;
    g->gFLACFlag  = 0;
    g->fdkAacProfile = 0;

    CString enc = basicSettings->m_EncoderType;
    if (enc == "Opus") {
        g->gOpusFlag = 1;
        strcpy(g->gEncodeType, "Opus");
    } else if (enc == "AAC++") {
        g->gAACFlag = 1;  g->fdkAacProfile = 29;
        strcpy(g->gEncodeType, "AAC++");
    } else if (enc == "AAC+") {
        g->gAACFlag = 1;  g->fdkAacProfile = 5;
        strcpy(g->gEncodeType, "AAC+");
    } else if (enc == "AAC-LC") {
        g->gAACFlag = 1;  g->fdkAacProfile = 2;
        strcpy(g->gEncodeType, "AAC-LC");
    } else if (enc == "OggVorbis") {
        g->gOggFlag = 1;
        strcpy(g->gEncodeType, "OggVorbis");
    } else if (enc == "Ogg FLAC") {
        g->gFLACFlag = 1;
        strcpy(g->gEncodeType, "Ogg FLAC");
    } else if (enc == "MP3 -- LAME (built-in)" || enc == "MP3 Lame" || enc == "MP3 -- ACM") {
        g->gLAMEFlag = 1;
        strcpy(g->gEncodeType, "MP3");
    } else if (enc == "AAC Plus") {   /* legacy */
        g->gAACPFlag = 1;
        strcpy(g->gEncodeType, "AAC Plus");
    } else if (enc == "AAC") {        /* legacy */
        g->gAACFlag = 1;
        strcpy(g->gEncodeType, "AAC");
    }

	if (basicSettings->m_UseBitrate) {
		g->gOggBitQualFlag = 1;
	}
	else {
		g->gOggBitQualFlag = 0;
	}
	if (basicSettings->m_JointStereo) {
		g->LAMEJointStereoFlag = 1;
	}
	else {
		g->LAMEJointStereoFlag = 0;
	}

    /* gEncodeType already set to canonical string in the if-else chain above */

    if (g->gAACFlag) {
        strcpy(g->gAACQuality, LPCSTR(basicSettings->m_Quality));
    }
    if (g->gLAMEFlag) {
        strcpy(g->gOggQuality, LPCSTR(basicSettings->m_Quality));
    }
    if (g->gOggFlag) {
        strcpy(g->gOggQuality, LPCSTR(basicSettings->m_Quality));
    }
    strcpy(g->gMountpoint, LPCSTR(basicSettings->m_Mountpoint));
    strcpy(g->gPassword, LPCSTR(basicSettings->m_Password));

    g->gReconnectSec = atoi(LPCSTR(basicSettings->m_ReconnectSecs));
    strcpy(g->gServer, LPCSTR(basicSettings->m_ServerIP));
    strcpy(g->gPort, LPCSTR(basicSettings->m_ServerPort));

    if (basicSettings->m_ServerType == "Shoutcast") {
        g->gShoutcastFlag = 1;
        g->gIcecast2Flag = 0;
    }
    if (basicSettings->m_ServerType == "Icecast2") {
        g->gShoutcastFlag = 0;
        g->gIcecast2Flag = 1;
    }
    if (basicSettings->m_ServerType == "Mcaster1DNAS") {
        // Mcaster1DNAS uses the Icecast2 protocol
        g->gShoutcastFlag = 0;
        g->gIcecast2Flag = 1;
    }
    strcpy(g->gServerType, LPCSTR(basicSettings->m_ServerType));

    ypSettings->UpdateData(TRUE);
    if (ypSettings->m_Public) {
        g->gPubServ = 1;
    }
    else {
        g->gPubServ = 0;
    }

    strcpy(g->gServDesc, LPCSTR(ypSettings->m_StreamDesc));
    strcpy(g->gServName, LPCSTR(ypSettings->m_StreamName));
    strcpy(g->gServGenre, LPCSTR(ypSettings->m_StreamGenre));
    strcpy(g->gServURL, LPCSTR(ypSettings->m_StreamURL));
    strcpy(g->gServAIM, LPCSTR(ypSettings->m_StreamAIM));
    strcpy(g->gServICQ, LPCSTR(ypSettings->m_StreamICQ));
    strcpy(g->gServIRC, LPCSTR(ypSettings->m_StreamIRC));

    /* Advanced Settings
    	CString	m_ArchiveDirectory;
	CString	m_Logfile;
	CString	m_Loglevel;
	BOOL	m_Savestream;
	BOOL	m_Savewav;
    */
    advSettings->UpdateData(TRUE);

    g->gSaveDirectoryFlag = advSettings->m_Savestream;
    strcpy(g->gSaveDirectory, LPCSTR(advSettings->m_ArchiveDirectory));
    g->gLogLevel = atoi(LPCSTR(advSettings->m_Loglevel));
	strcpy(g->gLogFile, LPCSTR(advSettings->m_Logfile));
    g->gSaveAsWAV = advSettings->m_Savewav;

}

void CConfig::OnClose()
{
    CResizableDialog::OnClose();
}

void CConfig::OnOK() 
{
	// TODO: Add extra validation here
	basicSettings->UpdateData(TRUE);
	ypSettings->UpdateData(TRUE);
	advSettings->UpdateData(TRUE);
    CMainWindow *pwin = (CMainWindow *)parentDialog;
    pwin->ProcessConfigDone(currentEnc, this);
    CDialog::OnOK();
}

void CConfig::OnCancel() 
{
	// TODO: Add extra cleanup here
    CMainWindow *pwin = (CMainWindow *)parentDialog;
    pwin->ProcessConfigDone(-1, this);
    CDialog::OnCancel();
}
