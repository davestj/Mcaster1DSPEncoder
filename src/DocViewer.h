#if !defined(AFX_DOCVIEWER_H__INCLUDED_)
#define AFX_DOCVIEWER_H__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// DocViewer.h : Modeless embedded documentation browser
//
// Uses WebBrowser ActiveX (CLSID_WebBrowser) hosted via MFC CreateControl.
// Opened from MainWindow Help menu; stays open while encoding.
//

/////////////////////////////////////////////////////////////////////////////
// CDocViewerDlg — modeless documentation viewer

class CDocViewerDlg : public CResizableDialog
{
public:
    CDocViewerDlg(CWnd *pParent = NULL);

    // Called by MainWindow to navigate to a specific page index (0-2)
    void NavigateTo(int page);

    // Page URLs: 0=ICY 2.2 Spec, 1=Encoder Guide, 2=Roadmap
    static const char * const s_urls[3];

    enum { IDD = IDD_DOC_VIEWER };

private:
    CTabCtrl  m_tabs;
    CWnd      m_wndBrowser;    // MFC OLE control container host
    int       m_currentPage;

    DECLARE_MESSAGE_MAP()
    virtual BOOL OnInitDialog();
    virtual void PostNcDestroy();                   // delete this — modeless cleanup

    afx_msg void OnClose();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnSelchangeTabs(NMHDR *pNMHDR, LRESULT *pResult);
};

//{{AFX_INSERT_LOCATION}}
#endif // !defined(AFX_DOCVIEWER_H__INCLUDED_)
