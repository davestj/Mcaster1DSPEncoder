#if !defined(AFX_ICY22SETTINGS_H__INCLUDED_)
#define AFX_ICY22SETTINGS_H__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// ICY22Settings.h : ICY 2.2 Extended metadata config tab (Phase 5)
//
// Implements the full ICY-META v2.2 specification:
//   https://mcaster1.com/icy2-spec/index.html
//

/////////////////////////////////////////////////////////////////////////////
// CICY22Settings — dashboard tab (IDD_PROPPAGE_LARGE3)

class CICY22Settings : public CResizableDialog
{
// Construction
public:
    CICY22Settings(CWnd* pParent = NULL);

    void GenerateSessionID();   // auto-generate UUID for gICY22SessionID if blank

// Dialog Data
    //{{AFX_DATA(CICY22Settings)
    enum { IDD = IDD_PROPPAGE_LARGE3 };

    // Station Identity
    CString m_StationID;
    CString m_StationLogo;
    CString m_VerifyStatus;

    // Show / Programming
    CString m_ShowTitle;
    CString m_ShowStart;
    CString m_ShowEnd;
    CString m_NextShow;
    CString m_NextShowTime;
    CString m_ScheduleURL;
    BOOL    m_AutoDJ;
    CString m_PlaylistName;

    // DJ / Host
    CString m_DJHandle;
    CString m_DJBio;
    CString m_DJGenre;
    CString m_DJRating;

    // Social & Discovery
    CString m_CreatorHandle;
    CString m_SocialTwitter;
    CString m_SocialTwitch;
    CString m_SocialIG;
    CString m_SocialTikTok;
    CString m_SocialYouTube;
    CString m_SocialFacebook;
    CString m_SocialLinkedIn;
    CString m_SocialLinktree;
    CString m_Emoji;
    CString m_Hashtags;

    // Listener Engagement
    BOOL    m_RequestEnabled;
    CString m_RequestURL;
    CString m_ChatURL;
    CString m_TipURL;
    CString m_EventsURL;

    // Broadcast Distribution
    CString m_CrosspostPlatforms;
    CString m_SessionID;
    CString m_CDNRegion;
    CString m_RelayOrigin;

    // Compliance / Content Flags
    BOOL    m_NSFW;
    BOOL    m_AIGenerator;
    CString m_GeoRegion;
    CString m_LicenseType;
    BOOL    m_RoyaltyFree;
    CString m_LicenseTerritory;

    // Station Notice
    CString m_NoticeText;
    CString m_NoticeURL;
    CString m_NoticeExpires;

    // Video / Simulcast
    CString m_VideoType;
    CString m_VideoLink;
    CString m_VideoTitle;
    CString m_VideoPoster;
    CString m_VideoPlatform;
    BOOL    m_VideoLive;
    CString m_VideoCodec;
    CString m_VideoFPS;
    CString m_VideoResolution;
    CString m_VideoRating;
    BOOL    m_VideoNSFW;

    // Audio Technical
    CString m_LoudnessLUFS;
    //}}AFX_DATA

// Overrides
    //{{AFX_VIRTUAL(CICY22Settings)
    protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    //}}AFX_VIRTUAL

// Implementation
protected:
    //{{AFX_MSG(CICY22Settings)
    virtual BOOL OnInitDialog();
    afx_msg void OnBtnStation();
    afx_msg void OnBtnDJ();
    afx_msg void OnBtnSocial();
    afx_msg void OnBtnEngage();
    afx_msg void OnBtnComply();
    afx_msg void OnBtnVideo();
    afx_msg void OnBtnGenSession();
    //}}AFX_MSG
    DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////
// CICY22StationDlg — Station & Show popup (IDD_ICY22_STATION)

class CICY22StationDlg : public CResizableDialog
{
public:
    CICY22StationDlg(CICY22Settings *pParent, CWnd *pWndParent = NULL);
    enum { IDD = IDD_ICY22_STATION };

protected:
    CICY22Settings *m_pICY22;
    CComboBox m_VerifyStatusCtrl;

    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();

    DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////
// CICY22DJDlg — DJ & Host popup (IDD_ICY22_DJ)

class CICY22DJDlg : public CResizableDialog
{
public:
    CICY22DJDlg(CICY22Settings *pParent, CWnd *pWndParent = NULL);
    enum { IDD = IDD_ICY22_DJ };

protected:
    CICY22Settings *m_pICY22;

    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();

    DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////
// CICY22SocialDlg — Social & Discovery popup (IDD_ICY22_SOCIAL)

class CICY22SocialDlg : public CResizableDialog
{
public:
    CICY22SocialDlg(CICY22Settings *pParent, CWnd *pWndParent = NULL);
    enum { IDD = IDD_ICY22_SOCIAL };

protected:
    CICY22Settings *m_pICY22;

    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();

    DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////
// CICY22EngageDlg — Listener Engagement popup (IDD_ICY22_ENGAGE)

class CICY22EngageDlg : public CResizableDialog
{
public:
    CICY22EngageDlg(CICY22Settings *pParent, CWnd *pWndParent = NULL);
    enum { IDD = IDD_ICY22_ENGAGE };

protected:
    CICY22Settings *m_pICY22;

    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();

    DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////
// CICY22ComplyDlg — Compliance & Legal popup (IDD_ICY22_COMPLY)

class CICY22ComplyDlg : public CResizableDialog
{
public:
    CICY22ComplyDlg(CICY22Settings *pParent, CWnd *pWndParent = NULL);
    enum { IDD = IDD_ICY22_COMPLY };

protected:
    CICY22Settings *m_pICY22;
    CComboBox m_LicenseTypeCtrl;

    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();

    DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////
// CICY22VideoDlg — Video / Simulcast popup (IDD_ICY22_VIDEO)

class CICY22VideoDlg : public CResizableDialog
{
public:
    CICY22VideoDlg(CICY22Settings *pParent, CWnd *pWndParent = NULL);
    enum { IDD = IDD_ICY22_VIDEO };

protected:
    CICY22Settings *m_pICY22;
    CComboBox m_VideoTypeCtrl;
    CComboBox m_VideoPlatformCtrl;

    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnOK();

    DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
#endif // !defined(AFX_ICY22SETTINGS_H__INCLUDED_)
