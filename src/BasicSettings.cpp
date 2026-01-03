// BasicSettings.cpp : implementation file
//

#include "stdafx.h"
#include "mcaster1dspencoder.h"
#include "BasicSettings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CBasicSettings dialog


CBasicSettings::CBasicSettings(CWnd* pParent /*=NULL*/)
	: CResizableDialog(CBasicSettings::IDD, pParent)
{
	//{{AFX_DATA_INIT(CBasicSettings)
	m_Bitrate = _T("");
	m_Channels = _T("");
	m_EncoderType = _T("");
	m_Mountpoint = _T("");
	m_Password = _T("");
	m_Quality = _T("");
	m_ReconnectSecs = _T("");
	m_Samplerate = _T("");
	m_ServerIP = _T("");
	m_ServerPort = _T("");
	m_ServerType = _T("");
	m_LamePreset = _T("");
	m_UseBitrate = FALSE;
	m_JointStereo = FALSE;
	//}}AFX_DATA_INIT
}


void CBasicSettings::DoDataExchange(CDataExchange* pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CBasicSettings)
	DDX_Control(pDX, IDC_JOINTSTEREO, m_JointStereoCtrl);
	DDX_Control(pDX, IDC_USEBITRATE, m_UseBitrateCtrl);
	DDX_Control(pDX, IDC_QUALITY, m_QualityCtrl);
	DDX_Control(pDX, IDC_BITRATE, m_BitrateCtrl);
	DDX_Control(pDX, IDC_SERVER_TYPE, m_ServerTypeCtrl);
	DDX_Control(pDX, IDC_ENCODER_TYPE, m_EncoderTypeCtrl);
	DDX_Text(pDX, IDC_BITRATE, m_Bitrate);
	DDX_Text(pDX, IDC_CHANNELS, m_Channels);
	DDX_CBString(pDX, IDC_ENCODER_TYPE, m_EncoderType);
	DDX_Text(pDX, IDC_MOUNTPOINT, m_Mountpoint);
	DDX_Text(pDX, IDC_PASSWORD, m_Password);
	DDX_Text(pDX, IDC_QUALITY, m_Quality);
	DDX_Text(pDX, IDC_RECONNECTSECS, m_ReconnectSecs);
	DDX_Text(pDX, IDC_SAMPLERATE, m_Samplerate);
	DDX_Text(pDX, IDC_SERVER_IP, m_ServerIP);
	DDX_Text(pDX, IDC_SERVER_PORT, m_ServerPort);
	DDX_CBString(pDX, IDC_SERVER_TYPE, m_ServerType);
	DDX_Check(pDX, IDC_USEBITRATE, m_UseBitrate);
	DDX_Check(pDX, IDC_JOINTSTEREO, m_JointStereo);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CBasicSettings, CResizableDialog)
	//{{AFX_MSG_MAP(CBasicSettings)
	ON_CBN_SELCHANGE(IDC_ENCODER_TYPE, OnSelchangeEncoderType)
	ON_CBN_SELENDOK(IDC_ENCODER_TYPE, OnSelendokEncoderType)
	ON_BN_CLICKED(IDC_USEBITRATE, OnUsebitrate)
	ON_BN_CLICKED(IDC_JOINTSTEREO, OnJointstereo)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CBasicSettings message handlers

BOOL CBasicSettings::OnInitDialog()
{
	CResizableDialog::OnInitDialog();

	m_ServerTypeCtrl.AddString(_T("Icecast2"));
	m_ServerTypeCtrl.AddString(_T("Shoutcast"));
	m_ServerTypeCtrl.AddString(_T("Mcaster1DNAS"));

	m_EncoderTypeCtrl.ResetContent();
	m_EncoderTypeCtrl.AddString(_T("OggVorbis"));
	m_EncoderTypeCtrl.AddString(_T("Ogg FLAC"));
	m_EncoderTypeCtrl.AddString(_T("MP3 -- LAME (built-in)"));
	m_EncoderTypeCtrl.AddString(_T("Opus"));
	m_EncoderTypeCtrl.AddString(_T("AAC-LC"));
	m_EncoderTypeCtrl.AddString(_T("AAC+"));
	m_EncoderTypeCtrl.AddString(_T("AAC++"));
	m_EncoderTypeCtrl.AddString(_T("MP3 -- ACM"));

	// ResizableLib anchors — groupbox fills page; right-side combos grow; labels stay left
	AddAnchor(IDC_GENERAL_GROUP, TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_ENCODER_TYPE,  TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_SERVER_TYPE,   TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_SERVER_IP,     TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_PASSWORD,      TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_MOUNTPOINT,    TOP_LEFT, TOP_RIGHT);
	AddAllOtherAnchors(TOP_LEFT);

    return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}


void CBasicSettings::UpdateFields() {
    m_BitrateCtrl.EnableWindow(TRUE);
    m_QualityCtrl.EnableWindow(TRUE);
	m_UseBitrateCtrl.EnableWindow(FALSE);
    if (m_EncoderType == "AAC Plus") {
	    m_BitrateCtrl.EnableWindow(TRUE);
	    m_QualityCtrl.EnableWindow(FALSE);
		m_JointStereoCtrl.EnableWindow(FALSE);
    }
    if (m_EncoderType == "AAC" || m_EncoderType == "AAC-LC" ||
        m_EncoderType == "AAC+" || m_EncoderType == "AAC++") {
		if (m_UseBitrate) {
	        m_BitrateCtrl.EnableWindow(TRUE);
	        m_QualityCtrl.EnableWindow(FALSE);
		}
		else {
	        m_QualityCtrl.EnableWindow(TRUE);
	        m_BitrateCtrl.EnableWindow(FALSE);
		}
		m_JointStereoCtrl.EnableWindow(FALSE);
    }
    if (m_EncoderType == "OggVorbis") {
		m_UseBitrateCtrl.EnableWindow(TRUE);
		if (m_UseBitrate) {
	        m_BitrateCtrl.EnableWindow(TRUE);
	        m_QualityCtrl.EnableWindow(FALSE);
		}
		else {
	        m_QualityCtrl.EnableWindow(TRUE);
	        m_BitrateCtrl.EnableWindow(FALSE);
		}
		m_JointStereoCtrl.EnableWindow(FALSE);
    }
    if (m_EncoderType == "MP3 Lame" || m_EncoderType == "MP3 -- LAME (built-in)" ||
        m_EncoderType == "MP3 -- ACM") {
        m_QualityCtrl.EnableWindow(FALSE);
		m_JointStereoCtrl.EnableWindow(TRUE);
    }
    if (m_EncoderType == "Ogg FLAC") {
	    m_BitrateCtrl.EnableWindow(FALSE);
	    m_QualityCtrl.EnableWindow(FALSE);
		m_JointStereoCtrl.EnableWindow(FALSE);
    }
    if (m_EncoderType == "Opus") {
        m_BitrateCtrl.EnableWindow(TRUE);
        m_QualityCtrl.EnableWindow(FALSE);
        m_JointStereoCtrl.EnableWindow(FALSE);

        /* Opus input rate is fixed at 48 kHz (PortAudio stream rate).
         * Force the field to 48000 and lock it so users can't enter an
         * invalid rate that would cause encoder init to fail. */
        CWnd *pSR = GetDlgItem(IDC_SAMPLERATE);
        if(pSR) {
            pSR->SetWindowText(_T("48000"));
            pSR->EnableWindow(FALSE);
        }

        /* Opus mapping family 0 supports only mono (1) or stereo (2).
         * If the user had typed something else, clamp it to 2. */
        CWnd *pCh = GetDlgItem(IDC_CHANNELS);
        if(pCh) {
            CString chStr;
            pCh->GetWindowText(chStr);
            int ch = _ttoi(chStr);
            if(ch < 1 || ch > 2)
                pCh->SetWindowText(_T("2"));
        }
    } else {
        /* Restore sample-rate field to editable for all other formats */
        CWnd *pSR = GetDlgItem(IDC_SAMPLERATE);
        if(pSR) pSR->EnableWindow(TRUE);
    }
}
void CBasicSettings::OnSelchangeEncoderType() 
{
	UpdateFields();
}

void CBasicSettings::OnSelendokEncoderType() 
{
	//UpdateData(TRUE);
    int selected = m_EncoderTypeCtrl.GetCurSel();
    CString  selectedString;
    m_EncoderTypeCtrl.GetLBText(m_EncoderTypeCtrl.GetCurSel(), selectedString);
    m_BitrateCtrl.EnableWindow(TRUE);
    m_QualityCtrl.EnableWindow(TRUE);
	m_EncoderType = selectedString;
	UpdateFields();

}

void CBasicSettings::OnUsebitrate() 
{
	// TODO: Add your control notification handler code here
	UpdateData(TRUE);
	UpdateFields();
}

void CBasicSettings::OnJointstereo() 
{
	// TODO: Add your control notification handler code here
	UpdateData(TRUE);
	UpdateFields();
	
}
