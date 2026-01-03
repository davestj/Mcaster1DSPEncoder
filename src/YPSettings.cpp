// YPSettings.cpp : implementation file
//

#include "stdafx.h"
#include "mcaster1dspencoder.h"
#include "YPSettings.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CYPSettings dialog


CYPSettings::CYPSettings(CWnd* pParent /*=NULL*/)
	: CResizableDialog(CYPSettings::IDD, pParent)
{
	//{{AFX_DATA_INIT(CYPSettings)
	m_Public = FALSE;
	m_StreamDesc = _T("");
	m_StreamGenre = _T("");
	m_StreamName = _T("");
	m_StreamURL = _T("");
	m_StreamICQ = _T("");
	m_StreamIRC = _T("");
	m_StreamAIM = _T("");
	//}}AFX_DATA_INIT
}


void CYPSettings::DoDataExchange(CDataExchange* pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CYPSettings)
	DDX_Control(pDX, IDC_IRC, m_StreamIRCCtrl);
	DDX_Control(pDX, IDC_ICQ, m_StreamICQCtrl);
	DDX_Control(pDX, IDC_AIM, m_StreamAIMCtrl);
	DDX_Control(pDX, IDC_STREAMURL, m_StreamURLCtrl);
	DDX_Control(pDX, IDC_STREAMNAME, m_StreamNameCtrl);
	DDX_Control(pDX, IDC_STREAMGENRE, m_StreamGenreCtrl);
	DDX_Control(pDX, IDC_STREAMDESC, m_StreamDescCtrl);
	DDX_Check(pDX, IDC_PUBLIC, m_Public);
	DDX_Text(pDX, IDC_STREAMDESC, m_StreamDesc);
	DDX_Text(pDX, IDC_STREAMGENRE, m_StreamGenre);
	DDX_Text(pDX, IDC_STREAMNAME, m_StreamName);
	DDX_Text(pDX, IDC_STREAMURL, m_StreamURL);
	DDX_Text(pDX, IDC_ICQ, m_StreamICQ);
	DDX_Text(pDX, IDC_IRC, m_StreamIRC);
	DDX_Text(pDX, IDC_AIM, m_StreamAIM);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CYPSettings, CResizableDialog)
	//{{AFX_MSG_MAP(CYPSettings)
	ON_BN_CLICKED(IDC_PUBLIC, OnPublic)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CYPSettings message handlers

void CYPSettings::OnPublic() 
{
    UpdateData(TRUE);
	// TODO: Add your control notification handler code here
    EnableDisable();
}

void CYPSettings::EnableDisable()
{
    if (m_Public) {
        m_StreamURLCtrl.EnableWindow(TRUE);
        m_StreamNameCtrl.EnableWindow(TRUE);
        m_StreamGenreCtrl.EnableWindow(TRUE);
        m_StreamDescCtrl.EnableWindow(TRUE);
        m_StreamAIMCtrl.EnableWindow(TRUE);
        m_StreamIRCCtrl.EnableWindow(TRUE);
        m_StreamICQCtrl.EnableWindow(TRUE);
    }
    else {
        //m_StreamURLCtrl.EnableWindow(FALSE); //original code that disables boxes when public is unchecked
        //m_StreamNameCtrl.EnableWindow(FALSE);
        //m_StreamGenreCtrl.EnableWindow(FALSE);
        //m_StreamDescCtrl.EnableWindow(FALSE);
        //m_StreamAIMCtrl.EnableWindow(FALSE);
        //m_StreamIRCCtrl.EnableWindow(FALSE);
        //m_StreamICQCtrl.EnableWindow(FALSE);
		m_StreamURLCtrl.EnableWindow(TRUE);
        m_StreamNameCtrl.EnableWindow(TRUE);
        m_StreamGenreCtrl.EnableWindow(TRUE);
        m_StreamDescCtrl.EnableWindow(TRUE);
        m_StreamAIMCtrl.EnableWindow(TRUE);
        m_StreamIRCCtrl.EnableWindow(TRUE);
        m_StreamICQCtrl.EnableWindow(TRUE);
    }
}
BOOL CYPSettings::OnInitDialog()
{
	CResizableDialog::OnInitDialog();

	// ResizableLib anchors — groupbox fills page; text fields stretch horizontally
	AddAnchor(IDC_YP_GROUP,    TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_STREAMNAME,  TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_STREAMDESC,  TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_STREAMURL,   TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_STREAMGENRE, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_AIM,         TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_ICQ,         TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_IRC,         TOP_LEFT, TOP_RIGHT);
	AddAllOtherAnchors(TOP_LEFT);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}
