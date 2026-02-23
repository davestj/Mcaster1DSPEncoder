// ICY22Settings.cpp : ICY 2.2 Extended metadata config tab — Phase 5
//
// Full ICY-META v2.2 spec: https://mcaster1.com/icy2-spec/index.html
//
// Phase 5.5: Reworked IDD_PROPPAGE_LARGE3 as a category-button dashboard.
// Each button opens a dedicated resizable popup dialog for that category.
//

#include "stdafx.h"
#include "mcaster1dspencoder.h"
#include "ICY22Settings.h"
#include <objbase.h>    // CoCreateGuid

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CICY22Settings — dashboard

CICY22Settings::CICY22Settings(CWnd* pParent /*=NULL*/)
    : CResizableDialog(CICY22Settings::IDD, pParent)
{
    //{{AFX_DATA_INIT(CICY22Settings)
    // Station Identity
    m_StationID = _T("");
    m_StationLogo = _T("");
    m_VerifyStatus = _T("unverified");
    // Show / Programming
    m_ShowTitle = _T("");
    m_ShowStart = _T("");
    m_ShowEnd = _T("");
    m_NextShow = _T("");
    m_NextShowTime = _T("");
    m_ScheduleURL = _T("");
    m_AutoDJ = FALSE;
    m_PlaylistName = _T("");
    // DJ / Host
    m_DJHandle = _T("");
    m_DJBio = _T("");
    m_DJGenre = _T("");
    m_DJRating = _T("");
    // Social & Discovery
    m_CreatorHandle = _T("");
    m_SocialTwitter = _T("");
    m_SocialTwitch = _T("");
    m_SocialIG = _T("");
    m_SocialTikTok = _T("");
    m_SocialYouTube = _T("");
    m_SocialFacebook = _T("");
    m_SocialLinkedIn = _T("");
    m_SocialLinktree = _T("");
    m_Emoji = _T("");
    m_Hashtags = _T("");
    // Listener Engagement
    m_RequestEnabled = FALSE;
    m_RequestURL = _T("");
    m_ChatURL = _T("");
    m_TipURL = _T("");
    m_EventsURL = _T("");
    // Broadcast Distribution
    m_CrosspostPlatforms = _T("");
    m_SessionID = _T("");
    m_CDNRegion = _T("");
    m_RelayOrigin = _T("");
    // Compliance
    m_NSFW = FALSE;
    m_AIGenerator = FALSE;
    m_GeoRegion = _T("");
    m_LicenseType = _T("all-rights-reserved");
    m_RoyaltyFree = FALSE;
    m_LicenseTerritory = _T("GLOBAL");
    // Station Notice
    m_NoticeText = _T("");
    m_NoticeURL = _T("");
    m_NoticeExpires = _T("");
    // Video / Simulcast
    m_VideoType = _T("live");
    m_VideoLink = _T("");
    m_VideoTitle = _T("");
    m_VideoPoster = _T("");
    m_VideoPlatform = _T("");
    m_VideoLive = FALSE;
    m_VideoCodec = _T("");
    m_VideoFPS = _T("");
    m_VideoResolution = _T("");
    m_VideoRating = _T("");
    m_VideoNSFW = FALSE;
    // Audio Technical
    m_LoudnessLUFS = _T("");
    //}}AFX_DATA_INIT
}

void CICY22Settings::DoDataExchange(CDataExchange* pDX)
{
    CResizableDialog::DoDataExchange(pDX);
    //{{AFX_DATA_MAP(CICY22Settings)
    // Dashboard only exchanges the Session ID field
    DDX_Text(pDX, IDC_ICY22_SESSION_ID, m_SessionID);
    //}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CICY22Settings, CResizableDialog)
    //{{AFX_MSG_MAP(CICY22Settings)
    ON_BN_CLICKED(IDC_ICY22_BTN_STATION, OnBtnStation)
    ON_BN_CLICKED(IDC_ICY22_BTN_DJ,      OnBtnDJ)
    ON_BN_CLICKED(IDC_ICY22_BTN_SOCIAL,  OnBtnSocial)
    ON_BN_CLICKED(IDC_ICY22_BTN_ENGAGE,  OnBtnEngage)
    ON_BN_CLICKED(IDC_ICY22_BTN_COMPLY,  OnBtnComply)
    ON_BN_CLICKED(IDC_ICY22_BTN_VIDEO,   OnBtnVideo)
    ON_BN_CLICKED(IDC_ICY22_GEN_SESSION, OnBtnGenSession)
    //}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CICY22Settings helpers

void CICY22Settings::GenerateSessionID()
{
    if (!m_SessionID.IsEmpty()) return; // don't overwrite user-set value

    GUID guid;
    if (CoCreateGuid(&guid) == S_OK) {
        CString s;
        s.Format(_T("%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X"),
            guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1],
            guid.Data4[2], guid.Data4[3], guid.Data4[4],
            guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        m_SessionID = s;
    }
}

/////////////////////////////////////////////////////////////////////////////
// CICY22Settings message handlers

BOOL CICY22Settings::OnInitDialog()
{
    CResizableDialog::OnInitDialog();

    // ResizableLib anchors — stretch Session ID field horizontally
    AddAnchor(IDC_ICY22_BTN_STATION, TOP_LEFT);
    AddAnchor(IDC_ICY22_BTN_DJ,      TOP_RIGHT);
    AddAnchor(IDC_ICY22_BTN_SOCIAL,  TOP_LEFT);
    AddAnchor(IDC_ICY22_BTN_ENGAGE,  TOP_RIGHT);
    AddAnchor(IDC_ICY22_BTN_COMPLY,  TOP_LEFT);
    AddAnchor(IDC_ICY22_BTN_VIDEO,   TOP_RIGHT);
    AddAnchor(IDC_ICY22_SESSION_ID,  TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_GEN_SESSION, TOP_RIGHT);

    return TRUE;
}

void CICY22Settings::OnBtnStation()
{
    UpdateData(TRUE);
    CICY22StationDlg dlg(this, GetParent());
    dlg.DoModal();
}

void CICY22Settings::OnBtnDJ()
{
    UpdateData(TRUE);
    CICY22DJDlg dlg(this, GetParent());
    dlg.DoModal();
}

void CICY22Settings::OnBtnSocial()
{
    UpdateData(TRUE);
    CICY22SocialDlg dlg(this, GetParent());
    dlg.DoModal();
}

void CICY22Settings::OnBtnEngage()
{
    UpdateData(TRUE);
    CICY22EngageDlg dlg(this, GetParent());
    dlg.DoModal();
}

void CICY22Settings::OnBtnComply()
{
    UpdateData(TRUE);
    CICY22ComplyDlg dlg(this, GetParent());
    dlg.DoModal();
}

void CICY22Settings::OnBtnVideo()
{
    UpdateData(TRUE);
    CICY22VideoDlg dlg(this, GetParent());
    dlg.DoModal();
}

void CICY22Settings::OnBtnGenSession()
{
    UpdateData(TRUE);
    m_SessionID = _T(""); // clear so GenerateSessionID() will fill it
    GenerateSessionID();
    UpdateData(FALSE);
}


/////////////////////////////////////////////////////////////////////////////
// CICY22StationDlg

CICY22StationDlg::CICY22StationDlg(CICY22Settings *pParent, CWnd *pWndParent)
    : CResizableDialog(CICY22StationDlg::IDD, pWndParent)
    , m_pICY22(pParent)
{
}

void CICY22StationDlg::DoDataExchange(CDataExchange* pDX)
{
    CResizableDialog::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_ICY22_STATION_ID,      m_pICY22->m_StationID);
    DDX_Text(pDX, IDC_ICY22_STATION_LOGO,    m_pICY22->m_StationLogo);
    DDX_Control(pDX, IDC_ICY22_VERIFY_STATUS, m_VerifyStatusCtrl);
    DDX_CBString(pDX, IDC_ICY22_VERIFY_STATUS, m_pICY22->m_VerifyStatus);
    DDX_Text(pDX, IDC_ICY22_SHOW_TITLE,      m_pICY22->m_ShowTitle);
    DDX_Text(pDX, IDC_ICY22_SHOW_START,      m_pICY22->m_ShowStart);
    DDX_Text(pDX, IDC_ICY22_SHOW_END,        m_pICY22->m_ShowEnd);
    DDX_Text(pDX, IDC_ICY22_NEXT_SHOW,       m_pICY22->m_NextShow);
    DDX_Text(pDX, IDC_ICY22_NEXT_SHOW_TIME,  m_pICY22->m_NextShowTime);
    DDX_Text(pDX, IDC_ICY22_SCHEDULE_URL,    m_pICY22->m_ScheduleURL);
    DDX_Check(pDX, IDC_ICY22_AUTODJ,         m_pICY22->m_AutoDJ);
    DDX_Text(pDX, IDC_ICY22_PLAYLIST_NAME,   m_pICY22->m_PlaylistName);
}

BEGIN_MESSAGE_MAP(CICY22StationDlg, CResizableDialog)
END_MESSAGE_MAP()

BOOL CICY22StationDlg::OnInitDialog()
{
    CResizableDialog::OnInitDialog();

    m_VerifyStatusCtrl.AddString(_T("unverified"));
    m_VerifyStatusCtrl.AddString(_T("pending"));
    m_VerifyStatusCtrl.AddString(_T("verified"));
    m_VerifyStatusCtrl.AddString(_T("gold"));
    m_VerifyStatusCtrl.SelectString(-1, m_pICY22->m_VerifyStatus);

    AddAnchor(IDC_ICY22_STATION_ID,      TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_STATION_LOGO,    TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_SHOW_TITLE,      TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_SCHEDULE_URL,    TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_PLAYLIST_NAME,   TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDOK,     BOTTOM_RIGHT);
    AddAnchor(IDCANCEL, BOTTOM_RIGHT);

    UpdateData(FALSE);
    return TRUE;
}

void CICY22StationDlg::OnOK()
{
    UpdateData(TRUE);
    CResizableDialog::OnOK();
}


/////////////////////////////////////////////////////////////////////////////
// CICY22DJDlg

CICY22DJDlg::CICY22DJDlg(CICY22Settings *pParent, CWnd *pWndParent)
    : CResizableDialog(CICY22DJDlg::IDD, pWndParent)
    , m_pICY22(pParent)
{
}

void CICY22DJDlg::DoDataExchange(CDataExchange* pDX)
{
    CResizableDialog::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_ICY22_DJ_HANDLE, m_pICY22->m_DJHandle);
    DDX_Text(pDX, IDC_ICY22_DJ_BIO,    m_pICY22->m_DJBio);
    DDX_Text(pDX, IDC_ICY22_DJ_GENRE,  m_pICY22->m_DJGenre);
    DDX_Text(pDX, IDC_ICY22_DJ_RATING, m_pICY22->m_DJRating);
}

BEGIN_MESSAGE_MAP(CICY22DJDlg, CResizableDialog)
END_MESSAGE_MAP()

BOOL CICY22DJDlg::OnInitDialog()
{
    CResizableDialog::OnInitDialog();

    AddAnchor(IDC_ICY22_DJ_HANDLE, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_DJ_BIO,    TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_DJ_GENRE,  TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDOK,     BOTTOM_RIGHT);
    AddAnchor(IDCANCEL, BOTTOM_RIGHT);

    UpdateData(FALSE);
    return TRUE;
}

void CICY22DJDlg::OnOK()
{
    UpdateData(TRUE);
    CResizableDialog::OnOK();
}


/////////////////////////////////////////////////////////////////////////////
// CICY22SocialDlg

CICY22SocialDlg::CICY22SocialDlg(CICY22Settings *pParent, CWnd *pWndParent)
    : CResizableDialog(CICY22SocialDlg::IDD, pWndParent)
    , m_pICY22(pParent)
{
}

void CICY22SocialDlg::DoDataExchange(CDataExchange* pDX)
{
    CResizableDialog::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_ICY22_CREATOR_HANDLE,  m_pICY22->m_CreatorHandle);
    DDX_Text(pDX, IDC_ICY22_EMOJI,           m_pICY22->m_Emoji);
    DDX_Text(pDX, IDC_ICY22_HASHTAGS,        m_pICY22->m_Hashtags);
    DDX_Text(pDX, IDC_ICY22_SOCIAL_TWITTER,  m_pICY22->m_SocialTwitter);
    DDX_Text(pDX, IDC_ICY22_SOCIAL_TWITCH,   m_pICY22->m_SocialTwitch);
    DDX_Text(pDX, IDC_ICY22_SOCIAL_IG,       m_pICY22->m_SocialIG);
    DDX_Text(pDX, IDC_ICY22_SOCIAL_TIKTOK,   m_pICY22->m_SocialTikTok);
    DDX_Text(pDX, IDC_ICY22_SOCIAL_YOUTUBE,  m_pICY22->m_SocialYouTube);
    DDX_Text(pDX, IDC_ICY22_SOCIAL_FACEBOOK, m_pICY22->m_SocialFacebook);
    DDX_Text(pDX, IDC_ICY22_SOCIAL_LINKEDIN, m_pICY22->m_SocialLinkedIn);
    DDX_Text(pDX, IDC_ICY22_SOCIAL_LINKTREE, m_pICY22->m_SocialLinktree);
}

BEGIN_MESSAGE_MAP(CICY22SocialDlg, CResizableDialog)
END_MESSAGE_MAP()

BOOL CICY22SocialDlg::OnInitDialog()
{
    CResizableDialog::OnInitDialog();

    AddAnchor(IDC_ICY22_HASHTAGS,        TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_SOCIAL_YOUTUBE,  TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_SOCIAL_FACEBOOK, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_SOCIAL_LINKEDIN, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_SOCIAL_LINKTREE, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDOK,     BOTTOM_RIGHT);
    AddAnchor(IDCANCEL, BOTTOM_RIGHT);

    UpdateData(FALSE);
    return TRUE;
}

void CICY22SocialDlg::OnOK()
{
    UpdateData(TRUE);
    CResizableDialog::OnOK();
}


/////////////////////////////////////////////////////////////////////////////
// CICY22EngageDlg

CICY22EngageDlg::CICY22EngageDlg(CICY22Settings *pParent, CWnd *pWndParent)
    : CResizableDialog(CICY22EngageDlg::IDD, pWndParent)
    , m_pICY22(pParent)
{
}

void CICY22EngageDlg::DoDataExchange(CDataExchange* pDX)
{
    CResizableDialog::DoDataExchange(pDX);
    DDX_Check(pDX, IDC_ICY22_REQUEST_ENABLED, m_pICY22->m_RequestEnabled);
    DDX_Text(pDX, IDC_ICY22_REQUEST_URL,      m_pICY22->m_RequestURL);
    DDX_Text(pDX, IDC_ICY22_CHAT_URL,         m_pICY22->m_ChatURL);
    DDX_Text(pDX, IDC_ICY22_TIP_URL,          m_pICY22->m_TipURL);
    DDX_Text(pDX, IDC_ICY22_EVENTS_URL,       m_pICY22->m_EventsURL);
    DDX_Text(pDX, IDC_ICY22_CROSSPOST,        m_pICY22->m_CrosspostPlatforms);
    DDX_Text(pDX, IDC_ICY22_CDN_REGION,       m_pICY22->m_CDNRegion);
    DDX_Text(pDX, IDC_ICY22_RELAY_ORIGIN,     m_pICY22->m_RelayOrigin);
}

BEGIN_MESSAGE_MAP(CICY22EngageDlg, CResizableDialog)
END_MESSAGE_MAP()

BOOL CICY22EngageDlg::OnInitDialog()
{
    CResizableDialog::OnInitDialog();

    AddAnchor(IDC_ICY22_REQUEST_URL,  TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_EVENTS_URL,   TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_CROSSPOST,    TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_RELAY_ORIGIN, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDOK,     BOTTOM_RIGHT);
    AddAnchor(IDCANCEL, BOTTOM_RIGHT);

    UpdateData(FALSE);
    return TRUE;
}

void CICY22EngageDlg::OnOK()
{
    UpdateData(TRUE);
    CResizableDialog::OnOK();
}


/////////////////////////////////////////////////////////////////////////////
// CICY22ComplyDlg

CICY22ComplyDlg::CICY22ComplyDlg(CICY22Settings *pParent, CWnd *pWndParent)
    : CResizableDialog(CICY22ComplyDlg::IDD, pWndParent)
    , m_pICY22(pParent)
{
}

void CICY22ComplyDlg::DoDataExchange(CDataExchange* pDX)
{
    CResizableDialog::DoDataExchange(pDX);
    DDX_Check(pDX, IDC_ICY22_NSFW,           m_pICY22->m_NSFW);
    DDX_Check(pDX, IDC_ICY22_AI_GENERATOR,   m_pICY22->m_AIGenerator);
    DDX_Check(pDX, IDC_ICY22_ROYALTY_FREE,   m_pICY22->m_RoyaltyFree);
    DDX_Control(pDX, IDC_ICY22_LICENSE_TYPE, m_LicenseTypeCtrl);
    DDX_CBString(pDX, IDC_ICY22_LICENSE_TYPE, m_pICY22->m_LicenseType);
    DDX_Text(pDX, IDC_ICY22_LICENSE_TERRITORY, m_pICY22->m_LicenseTerritory);
    DDX_Text(pDX, IDC_ICY22_GEO_REGION,      m_pICY22->m_GeoRegion);
    DDX_Text(pDX, IDC_ICY22_NOTICE_TEXT,     m_pICY22->m_NoticeText);
    DDX_Text(pDX, IDC_ICY22_NOTICE_URL,      m_pICY22->m_NoticeURL);
    DDX_Text(pDX, IDC_ICY22_NOTICE_EXPIRES,  m_pICY22->m_NoticeExpires);
    DDX_Text(pDX, IDC_ICY22_LOUDNESS,        m_pICY22->m_LoudnessLUFS);
}

BEGIN_MESSAGE_MAP(CICY22ComplyDlg, CResizableDialog)
END_MESSAGE_MAP()

BOOL CICY22ComplyDlg::OnInitDialog()
{
    CResizableDialog::OnInitDialog();

    m_LicenseTypeCtrl.AddString(_T("all-rights-reserved"));
    m_LicenseTypeCtrl.AddString(_T("cc-by"));
    m_LicenseTypeCtrl.AddString(_T("cc-by-sa"));
    m_LicenseTypeCtrl.AddString(_T("cc0"));
    m_LicenseTypeCtrl.AddString(_T("pro-licensed"));
    m_LicenseTypeCtrl.SelectString(-1, m_pICY22->m_LicenseType);

    AddAnchor(IDC_ICY22_NOTICE_TEXT, TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_NOTICE_URL,  TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDOK,     BOTTOM_RIGHT);
    AddAnchor(IDCANCEL, BOTTOM_RIGHT);

    UpdateData(FALSE);
    return TRUE;
}

void CICY22ComplyDlg::OnOK()
{
    UpdateData(TRUE);
    CResizableDialog::OnOK();
}


/////////////////////////////////////////////////////////////////////////////
// CICY22VideoDlg

CICY22VideoDlg::CICY22VideoDlg(CICY22Settings *pParent, CWnd *pWndParent)
    : CResizableDialog(CICY22VideoDlg::IDD, pWndParent)
    , m_pICY22(pParent)
{
}

void CICY22VideoDlg::DoDataExchange(CDataExchange* pDX)
{
    CResizableDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_ICY22_VIDEO_TYPE,     m_VideoTypeCtrl);
    DDX_CBString(pDX, IDC_ICY22_VIDEO_TYPE,    m_pICY22->m_VideoType);
    DDX_Control(pDX, IDC_ICY22_VIDEO_PLATFORM, m_VideoPlatformCtrl);
    DDX_CBString(pDX, IDC_ICY22_VIDEO_PLATFORM, m_pICY22->m_VideoPlatform);
    DDX_Check(pDX, IDC_ICY22_VIDEO_LIVE,       m_pICY22->m_VideoLive);
    DDX_Check(pDX, IDC_ICY22_VIDEO_NSFW,       m_pICY22->m_VideoNSFW);
    DDX_Text(pDX, IDC_ICY22_VIDEO_TITLE,       m_pICY22->m_VideoTitle);
    DDX_Text(pDX, IDC_ICY22_VIDEO_LINK,        m_pICY22->m_VideoLink);
    DDX_Text(pDX, IDC_ICY22_VIDEO_POSTER,      m_pICY22->m_VideoPoster);
    DDX_Text(pDX, IDC_ICY22_VIDEO_CODEC,       m_pICY22->m_VideoCodec);
    DDX_Text(pDX, IDC_ICY22_VIDEO_FPS,         m_pICY22->m_VideoFPS);
    DDX_Text(pDX, IDC_ICY22_VIDEO_RESOLUTION,  m_pICY22->m_VideoResolution);
    DDX_Text(pDX, IDC_ICY22_VIDEO_RATING,      m_pICY22->m_VideoRating);
}

BEGIN_MESSAGE_MAP(CICY22VideoDlg, CResizableDialog)
END_MESSAGE_MAP()

BOOL CICY22VideoDlg::OnInitDialog()
{
    CResizableDialog::OnInitDialog();

    m_VideoTypeCtrl.AddString(_T("live"));
    m_VideoTypeCtrl.AddString(_T("short"));
    m_VideoTypeCtrl.AddString(_T("clip"));
    m_VideoTypeCtrl.AddString(_T("trailer"));
    m_VideoTypeCtrl.AddString(_T("ad"));
    m_VideoTypeCtrl.SelectString(-1, m_pICY22->m_VideoType);

    m_VideoPlatformCtrl.AddString(_T("youtube"));
    m_VideoPlatformCtrl.AddString(_T("tiktok"));
    m_VideoPlatformCtrl.AddString(_T("twitch"));
    m_VideoPlatformCtrl.AddString(_T("kick"));
    m_VideoPlatformCtrl.AddString(_T("rumble"));
    m_VideoPlatformCtrl.AddString(_T("vimeo"));
    m_VideoPlatformCtrl.AddString(_T("custom"));
    if (!m_pICY22->m_VideoPlatform.IsEmpty())
        m_VideoPlatformCtrl.SelectString(-1, m_pICY22->m_VideoPlatform);

    AddAnchor(IDC_ICY22_VIDEO_TITLE,      TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_VIDEO_LINK,       TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDC_ICY22_VIDEO_POSTER,     TOP_LEFT, TOP_RIGHT);
    AddAnchor(IDOK,     BOTTOM_RIGHT);
    AddAnchor(IDCANCEL, BOTTOM_RIGHT);

    UpdateData(FALSE);
    return TRUE;
}

void CICY22VideoDlg::OnOK()
{
    UpdateData(TRUE);
    CResizableDialog::OnOK();
}
