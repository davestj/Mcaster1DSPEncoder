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

    basicSettings  = new CBasicSettings();
    ypSettings     = new CYPSettings();
    advSettings    = new CAdvancedSettings();
    icy22Settings  = new CICY22Settings();
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
    if (icy22Settings) {
        delete icy22Settings;
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
	ON_MESSAGE(WM_SET_PODCAST_FROM_YP, OnSetPodcastFromYP)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CConfig message handlers

BOOL CConfig::OnInitDialog()
{
	CResizableDialog::OnInitDialog();

	m_TabCtrl.InsertItem(0, _T("Basic Settings"));
	m_TabCtrl.InsertItem(1, _T("YP Settings"));
	m_TabCtrl.InsertItem(2, _T("Podcast Settings"));
	m_TabCtrl.InsertItem(3, _T("Extended Details"));

    basicSettings->Create((UINT)IDD_PROPPAGE_LARGE,  this);
    ypSettings->Create((UINT)IDD_PROPPAGE_LARGE1,    this);
    advSettings->Create((UINT)IDD_PROPPAGE_LARGE2,   this);
    icy22Settings->Create((UINT)IDD_PROPPAGE_LARGE3, this);

    basicSettings->ShowWindow(SW_SHOW);
    ypSettings->ShowWindow(SW_HIDE);
    advSettings->ShowWindow(SW_HIDE);
    icy22Settings->ShowWindow(SW_HIDE);

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
		if (icy22Settings && IsWindow(icy22Settings->m_hWnd))
			icy22Settings->MoveWindow(&rcTab, FALSE);
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
        icy22Settings->ShowWindow(SW_HIDE);
    }
    if (selected == 1) {
        ypSettings->ShowWindow(SW_SHOW);
        basicSettings->ShowWindow(SW_HIDE);
        advSettings->ShowWindow(SW_HIDE);
        icy22Settings->ShowWindow(SW_HIDE);
    }
    if (selected == 2) {
        basicSettings->ShowWindow(SW_HIDE);
        ypSettings->ShowWindow(SW_HIDE);
        advSettings->ShowWindow(SW_SHOW);
        icy22Settings->ShowWindow(SW_HIDE);
    }
    if (selected == 3) {
        basicSettings->ShowWindow(SW_HIDE);
        ypSettings->ShowWindow(SW_HIDE);
        advSettings->ShowWindow(SW_HIDE);
        icy22Settings->ShowWindow(SW_SHOW);
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
    advSettings->m_Savestream        = g->gSaveDirectoryFlag;
    advSettings->m_ArchiveDirectory  = g->gSaveDirectory;
    sprintf(buf, "%d", g->gLogLevel);
    advSettings->m_Loglevel          = buf;
	advSettings->m_Logfile           = g->gLogFile;
    advSettings->m_Savewav           = g->gSaveAsWAV;
    // Podcast RSS
    advSettings->m_GenerateRSS       = g->gGenerateRSS;
    advSettings->m_RSSUseYP          = g->gRSSUseYPSettings;
    advSettings->m_PodcastTitle      = g->gPodcastTitle;
    advSettings->m_PodcastAuthor     = g->gPodcastAuthor;
    advSettings->m_PodcastCategory   = g->gPodcastCategory;
    advSettings->m_PodcastLanguage   = g->gPodcastLanguage;
    advSettings->m_PodcastCopyright  = g->gPodcastCopyright;
    advSettings->m_PodcastWebsite    = g->gPodcastWebsite;
    advSettings->m_PodcastCoverArt   = g->gPodcastCoverArt;
    advSettings->m_PodcastDescription= g->gPodcastDescription;
    advSettings->UpdateData(FALSE);
    advSettings->EnableDisable();

    // ICY 2.2 Extended
    icy22Settings->m_StationID           = g->gICY22StationID;
    icy22Settings->m_StationLogo         = g->gICY22StationLogo;
    icy22Settings->m_VerifyStatus        = g->gICY22VerifyStatus[0] ? g->gICY22VerifyStatus : "unverified";
    icy22Settings->m_ShowTitle           = g->gICY22ShowTitle;
    icy22Settings->m_ShowStart           = g->gICY22ShowStart;
    icy22Settings->m_ShowEnd             = g->gICY22ShowEnd;
    icy22Settings->m_NextShow            = g->gICY22NextShow;
    icy22Settings->m_NextShowTime        = g->gICY22NextShowTime;
    icy22Settings->m_ScheduleURL         = g->gICY22ScheduleURL;
    icy22Settings->m_AutoDJ              = g->gICY22AutoDJ;
    icy22Settings->m_PlaylistName        = g->gICY22PlaylistName;
    icy22Settings->m_DJHandle            = g->gICY22DJHandle;
    icy22Settings->m_DJBio               = g->gICY22DJBio;
    icy22Settings->m_DJGenre             = g->gICY22DJGenre;
    icy22Settings->m_DJRating            = g->gICY22DJRating;
    icy22Settings->m_CreatorHandle       = g->gICY22CreatorHandle;
    icy22Settings->m_SocialTwitter       = g->gICY22SocialTwitter;
    icy22Settings->m_SocialTwitch        = g->gICY22SocialTwitch;
    icy22Settings->m_SocialIG            = g->gICY22SocialIG;
    icy22Settings->m_SocialTikTok        = g->gICY22SocialTikTok;
    icy22Settings->m_SocialYouTube       = g->gICY22SocialYouTube;
    icy22Settings->m_SocialFacebook      = g->gICY22SocialFacebook;
    icy22Settings->m_SocialLinkedIn      = g->gICY22SocialLinkedIn;
    icy22Settings->m_SocialLinktree      = g->gICY22SocialLinktree;
    icy22Settings->m_Emoji               = g->gICY22Emoji;
    icy22Settings->m_Hashtags            = g->gICY22Hashtags;
    icy22Settings->m_RequestEnabled      = g->gICY22RequestEnabled;
    icy22Settings->m_RequestURL          = g->gICY22RequestURL;
    icy22Settings->m_ChatURL             = g->gICY22ChatURL;
    icy22Settings->m_TipURL              = g->gICY22TipURL;
    icy22Settings->m_EventsURL           = g->gICY22EventsURL;
    icy22Settings->m_CrosspostPlatforms  = g->gICY22CrosspostPlatforms;
    icy22Settings->m_SessionID           = g->gICY22SessionID;
    icy22Settings->m_CDNRegion           = g->gICY22CDNRegion;
    icy22Settings->m_RelayOrigin         = g->gICY22RelayOrigin;
    icy22Settings->m_NSFW                = g->gICY22NSFW;
    icy22Settings->m_AIGenerator         = g->gICY22AIGenerator;
    icy22Settings->m_GeoRegion           = g->gICY22GeoRegion;
    icy22Settings->m_LicenseType         = g->gICY22LicenseType[0] ? g->gICY22LicenseType : "all-rights-reserved";
    icy22Settings->m_RoyaltyFree         = g->gICY22RoyaltyFree;
    icy22Settings->m_LicenseTerritory    = g->gICY22LicenseTerritory[0] ? g->gICY22LicenseTerritory : "GLOBAL";
    icy22Settings->m_NoticeText          = g->gICY22NoticeText;
    icy22Settings->m_NoticeURL           = g->gICY22NoticeURL;
    icy22Settings->m_NoticeExpires       = g->gICY22NoticeExpires;
    icy22Settings->m_VideoType           = g->gICY22VideoType[0] ? g->gICY22VideoType : "live";
    icy22Settings->m_VideoLink           = g->gICY22VideoLink;
    icy22Settings->m_VideoTitle          = g->gICY22VideoTitle;
    icy22Settings->m_VideoPoster         = g->gICY22VideoPoster;
    icy22Settings->m_VideoPlatform       = g->gICY22VideoPlatform;
    icy22Settings->m_VideoLive           = g->gICY22VideoLive;
    icy22Settings->m_VideoCodec          = g->gICY22VideoCodec;
    icy22Settings->m_VideoFPS            = g->gICY22VideoFPS;
    icy22Settings->m_VideoResolution     = g->gICY22VideoResolution;
    icy22Settings->m_VideoRating         = g->gICY22VideoRating;
    icy22Settings->m_VideoNSFW           = g->gICY22VideoNSFW;
    icy22Settings->m_LoudnessLUFS        = g->gICY22LoudnessLUFS;
    icy22Settings->UpdateData(FALSE);
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
    // Podcast RSS
    g->gGenerateRSS       = advSettings->m_GenerateRSS;
    g->gRSSUseYPSettings  = advSettings->m_RSSUseYP;
    strncpy(g->gPodcastTitle,       LPCSTR(advSettings->m_PodcastTitle),       sizeof(g->gPodcastTitle)-1);
    strncpy(g->gPodcastAuthor,      LPCSTR(advSettings->m_PodcastAuthor),      sizeof(g->gPodcastAuthor)-1);
    strncpy(g->gPodcastCategory,    LPCSTR(advSettings->m_PodcastCategory),    sizeof(g->gPodcastCategory)-1);
    strncpy(g->gPodcastLanguage,    LPCSTR(advSettings->m_PodcastLanguage),    sizeof(g->gPodcastLanguage)-1);
    strncpy(g->gPodcastCopyright,   LPCSTR(advSettings->m_PodcastCopyright),   sizeof(g->gPodcastCopyright)-1);
    strncpy(g->gPodcastWebsite,     LPCSTR(advSettings->m_PodcastWebsite),     sizeof(g->gPodcastWebsite)-1);
    strncpy(g->gPodcastCoverArt,    LPCSTR(advSettings->m_PodcastCoverArt),    sizeof(g->gPodcastCoverArt)-1);
    strncpy(g->gPodcastDescription, LPCSTR(advSettings->m_PodcastDescription), sizeof(g->gPodcastDescription)-1);

    // ICY 2.2 Extended
    icy22Settings->UpdateData(TRUE);
    strncpy(g->gICY22StationID,          LPCSTR(icy22Settings->m_StationID),          sizeof(g->gICY22StationID)-1);
    strncpy(g->gICY22StationLogo,        LPCSTR(icy22Settings->m_StationLogo),        sizeof(g->gICY22StationLogo)-1);
    strncpy(g->gICY22VerifyStatus,       LPCSTR(icy22Settings->m_VerifyStatus),       sizeof(g->gICY22VerifyStatus)-1);
    strncpy(g->gICY22ShowTitle,          LPCSTR(icy22Settings->m_ShowTitle),          sizeof(g->gICY22ShowTitle)-1);
    strncpy(g->gICY22ShowStart,          LPCSTR(icy22Settings->m_ShowStart),          sizeof(g->gICY22ShowStart)-1);
    strncpy(g->gICY22ShowEnd,            LPCSTR(icy22Settings->m_ShowEnd),            sizeof(g->gICY22ShowEnd)-1);
    strncpy(g->gICY22NextShow,           LPCSTR(icy22Settings->m_NextShow),           sizeof(g->gICY22NextShow)-1);
    strncpy(g->gICY22NextShowTime,       LPCSTR(icy22Settings->m_NextShowTime),       sizeof(g->gICY22NextShowTime)-1);
    strncpy(g->gICY22ScheduleURL,        LPCSTR(icy22Settings->m_ScheduleURL),        sizeof(g->gICY22ScheduleURL)-1);
    g->gICY22AutoDJ =                    icy22Settings->m_AutoDJ;
    strncpy(g->gICY22PlaylistName,       LPCSTR(icy22Settings->m_PlaylistName),       sizeof(g->gICY22PlaylistName)-1);
    strncpy(g->gICY22DJHandle,           LPCSTR(icy22Settings->m_DJHandle),           sizeof(g->gICY22DJHandle)-1);
    strncpy(g->gICY22DJBio,              LPCSTR(icy22Settings->m_DJBio),              sizeof(g->gICY22DJBio)-1);
    strncpy(g->gICY22DJGenre,            LPCSTR(icy22Settings->m_DJGenre),            sizeof(g->gICY22DJGenre)-1);
    strncpy(g->gICY22DJRating,           LPCSTR(icy22Settings->m_DJRating),           sizeof(g->gICY22DJRating)-1);
    strncpy(g->gICY22CreatorHandle,      LPCSTR(icy22Settings->m_CreatorHandle),      sizeof(g->gICY22CreatorHandle)-1);
    strncpy(g->gICY22SocialTwitter,      LPCSTR(icy22Settings->m_SocialTwitter),      sizeof(g->gICY22SocialTwitter)-1);
    strncpy(g->gICY22SocialTwitch,       LPCSTR(icy22Settings->m_SocialTwitch),       sizeof(g->gICY22SocialTwitch)-1);
    strncpy(g->gICY22SocialIG,           LPCSTR(icy22Settings->m_SocialIG),           sizeof(g->gICY22SocialIG)-1);
    strncpy(g->gICY22SocialTikTok,       LPCSTR(icy22Settings->m_SocialTikTok),       sizeof(g->gICY22SocialTikTok)-1);
    strncpy(g->gICY22SocialYouTube,      LPCSTR(icy22Settings->m_SocialYouTube),      sizeof(g->gICY22SocialYouTube)-1);
    strncpy(g->gICY22SocialFacebook,     LPCSTR(icy22Settings->m_SocialFacebook),     sizeof(g->gICY22SocialFacebook)-1);
    strncpy(g->gICY22SocialLinkedIn,     LPCSTR(icy22Settings->m_SocialLinkedIn),     sizeof(g->gICY22SocialLinkedIn)-1);
    strncpy(g->gICY22SocialLinktree,     LPCSTR(icy22Settings->m_SocialLinktree),     sizeof(g->gICY22SocialLinktree)-1);
    strncpy(g->gICY22Emoji,              LPCSTR(icy22Settings->m_Emoji),              sizeof(g->gICY22Emoji)-1);
    strncpy(g->gICY22Hashtags,           LPCSTR(icy22Settings->m_Hashtags),           sizeof(g->gICY22Hashtags)-1);
    g->gICY22RequestEnabled =            icy22Settings->m_RequestEnabled;
    strncpy(g->gICY22RequestURL,         LPCSTR(icy22Settings->m_RequestURL),         sizeof(g->gICY22RequestURL)-1);
    strncpy(g->gICY22ChatURL,            LPCSTR(icy22Settings->m_ChatURL),            sizeof(g->gICY22ChatURL)-1);
    strncpy(g->gICY22TipURL,             LPCSTR(icy22Settings->m_TipURL),             sizeof(g->gICY22TipURL)-1);
    strncpy(g->gICY22EventsURL,          LPCSTR(icy22Settings->m_EventsURL),          sizeof(g->gICY22EventsURL)-1);
    strncpy(g->gICY22CrosspostPlatforms, LPCSTR(icy22Settings->m_CrosspostPlatforms), sizeof(g->gICY22CrosspostPlatforms)-1);
    strncpy(g->gICY22SessionID,          LPCSTR(icy22Settings->m_SessionID),          sizeof(g->gICY22SessionID)-1);
    strncpy(g->gICY22CDNRegion,          LPCSTR(icy22Settings->m_CDNRegion),          sizeof(g->gICY22CDNRegion)-1);
    strncpy(g->gICY22RelayOrigin,        LPCSTR(icy22Settings->m_RelayOrigin),        sizeof(g->gICY22RelayOrigin)-1);
    g->gICY22NSFW =                      icy22Settings->m_NSFW;
    g->gICY22AIGenerator =               icy22Settings->m_AIGenerator;
    strncpy(g->gICY22GeoRegion,          LPCSTR(icy22Settings->m_GeoRegion),          sizeof(g->gICY22GeoRegion)-1);
    strncpy(g->gICY22LicenseType,        LPCSTR(icy22Settings->m_LicenseType),        sizeof(g->gICY22LicenseType)-1);
    g->gICY22RoyaltyFree =               icy22Settings->m_RoyaltyFree;
    strncpy(g->gICY22LicenseTerritory,   LPCSTR(icy22Settings->m_LicenseTerritory),   sizeof(g->gICY22LicenseTerritory)-1);
    strncpy(g->gICY22NoticeText,         LPCSTR(icy22Settings->m_NoticeText),         sizeof(g->gICY22NoticeText)-1);
    strncpy(g->gICY22NoticeURL,          LPCSTR(icy22Settings->m_NoticeURL),          sizeof(g->gICY22NoticeURL)-1);
    strncpy(g->gICY22NoticeExpires,      LPCSTR(icy22Settings->m_NoticeExpires),      sizeof(g->gICY22NoticeExpires)-1);
    strncpy(g->gICY22VideoType,          LPCSTR(icy22Settings->m_VideoType),          sizeof(g->gICY22VideoType)-1);
    strncpy(g->gICY22VideoLink,          LPCSTR(icy22Settings->m_VideoLink),          sizeof(g->gICY22VideoLink)-1);
    strncpy(g->gICY22VideoTitle,         LPCSTR(icy22Settings->m_VideoTitle),         sizeof(g->gICY22VideoTitle)-1);
    strncpy(g->gICY22VideoPoster,        LPCSTR(icy22Settings->m_VideoPoster),        sizeof(g->gICY22VideoPoster)-1);
    strncpy(g->gICY22VideoPlatform,      LPCSTR(icy22Settings->m_VideoPlatform),      sizeof(g->gICY22VideoPlatform)-1);
    g->gICY22VideoLive =                 icy22Settings->m_VideoLive;
    strncpy(g->gICY22VideoCodec,         LPCSTR(icy22Settings->m_VideoCodec),         sizeof(g->gICY22VideoCodec)-1);
    strncpy(g->gICY22VideoFPS,           LPCSTR(icy22Settings->m_VideoFPS),           sizeof(g->gICY22VideoFPS)-1);
    strncpy(g->gICY22VideoResolution,    LPCSTR(icy22Settings->m_VideoResolution),    sizeof(g->gICY22VideoResolution)-1);
    strncpy(g->gICY22VideoRating,        LPCSTR(icy22Settings->m_VideoRating),        sizeof(g->gICY22VideoRating)-1);
    g->gICY22VideoNSFW =                 icy22Settings->m_VideoNSFW;
    strncpy(g->gICY22LoudnessLUFS,      LPCSTR(icy22Settings->m_LoudnessLUFS),       sizeof(g->gICY22LoudnessLUFS)-1);
}

void CConfig::OnClose()
{
    CResizableDialog::OnClose();
}

void CConfig::OnOK()
{
	basicSettings->UpdateData(TRUE);
	ypSettings->UpdateData(TRUE);
	advSettings->UpdateData(TRUE);
	icy22Settings->UpdateData(TRUE);
    CMainWindow *pwin = (CMainWindow *)parentDialog;
    pwin->ProcessConfigDone(currentEnc, this);
    CDialog::OnOK();
}

LRESULT CConfig::OnSetPodcastFromYP(WPARAM /*wParam*/, LPARAM lParam)
{
    // Populate podcast fields from YP Settings tab data
    // lParam is a pointer to the CAdvancedSettings page that sent the message
    CAdvancedSettings *pAdv = reinterpret_cast<CAdvancedSettings*>(lParam);
    if (!pAdv) return 0;

    // Only fill fields that are currently empty so we don't overwrite user data
    if (pAdv->m_PodcastTitle.IsEmpty())
        pAdv->m_PodcastTitle = ypSettings->m_StreamName;
    if (pAdv->m_PodcastDescription.IsEmpty())
        pAdv->m_PodcastDescription = ypSettings->m_StreamDesc;
    if (pAdv->m_PodcastWebsite.IsEmpty())
        pAdv->m_PodcastWebsite = ypSettings->m_StreamURL;
    if (pAdv->m_PodcastCategory.IsEmpty())
        pAdv->m_PodcastCategory = ypSettings->m_StreamGenre;

    pAdv->UpdateData(FALSE);
    return 0;
}

void CConfig::OnCancel() 
{
	// TODO: Add extra cleanup here
    CMainWindow *pwin = (CMainWindow *)parentDialog;
    pwin->ProcessConfigDone(-1, this);
    CDialog::OnCancel();
}
