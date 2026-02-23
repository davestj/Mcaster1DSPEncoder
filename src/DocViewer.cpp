// DocViewer.cpp : Modeless embedded documentation browser — Phase 5.5
//
// Hosts WebBrowser ActiveX (CLSID_WebBrowser) via MFC's CreateControl().
// AfxOleInit() (called in InitInstance) initialises COM before this dialog opens.
// The browser window is created programmatically in OnInitDialog.
// Owner (CMainWindow) manages the single modeless instance.
// PostNcDestroy() calls delete this for standard MFC modeless cleanup.
//

#include "stdafx.h"
#include "mcaster1dspencoder.h"
#include "DocViewer.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CLSID_WebBrowser = {8856F961-340A-11D0-A96B-00C04FD705A2}

static const CLSID CLSID_WebBrowserCtl =
    { 0x8856F961, 0x340A, 0x11D0,
      { 0xA9, 0x6B, 0x00, 0xC0, 0x4F, 0xD7, 0x05, 0xA2 } };

/////////////////////////////////////////////////////////////////////////////
// Page URL table (0=ICY 2.2 Spec, 1=Encoder Guide, 2=Roadmap)

const char * const CDocViewerDlg::s_urls[3] = {
    "https://mcaster1.com/icy2-spec/",
    "https://mcaster1.com/mcaster1dspencoder/",
    "https://mcaster1.com/mcaster1dspencoder/roadmap.html"
};

/////////////////////////////////////////////////////////////////////////////
// CDocViewerDlg

CDocViewerDlg::CDocViewerDlg(CWnd *pParent)
    : CResizableDialog(CDocViewerDlg::IDD, pParent)
    , m_currentPage(0)
{
}

BEGIN_MESSAGE_MAP(CDocViewerDlg, CResizableDialog)
    ON_WM_CLOSE()
    ON_WM_SIZE()
    ON_NOTIFY(TCN_SELCHANGE, IDC_DOC_TABS, OnSelchangeTabs)
END_MESSAGE_MAP()

BOOL CDocViewerDlg::OnInitDialog()
{
    CResizableDialog::OnInitDialog();

    // Subclass the tab control and add page tabs
    m_tabs.SubclassDlgItem(IDC_DOC_TABS, this);
    m_tabs.InsertItem(0, _T("ICY 2.2 Spec"));
    m_tabs.InsertItem(1, _T("Encoder Guide"));
    m_tabs.InsertItem(2, _T("Roadmap"));

    // Compute the browser rect: client area below the tab strip
    CRect rcClient;
    GetClientRect(&rcClient);
    CRect rcTabs;
    m_tabs.GetWindowRect(&rcTabs);
    ScreenToClient(&rcTabs);

    CRect rcBrowser;
    rcBrowser.left   = rcClient.left   + 7;
    rcBrowser.right  = rcClient.right  - 7;
    rcBrowser.top    = rcTabs.bottom   + 4;
    rcBrowser.bottom = rcClient.bottom - 7;

    // Create WebBrowser control via MFC's OLE container support.
    // COM is already initialised by AfxOleInit() in InitInstance.
    m_wndBrowser.CreateControl(
        CLSID_WebBrowserCtl,
        NULL,
        WS_CHILD | WS_VISIBLE,
        rcBrowser,
        this,
        IDC_DOC_BROWSER);

    // Suppress IE-style "Script Error" pop-up dialogs.
    // IWebBrowser2::put_Silent DISPID = 0x225 (same as CHtmlView::SetSilent).
    if (m_wndBrowser.GetSafeHwnd() != NULL) {
        static BYTE parmsSilent[] = VTS_BOOL;
        m_wndBrowser.InvokeHelper(0x225, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, parmsSilent, (BOOL)TRUE);
    }

    // ResizableLib anchors
    AddAnchor(IDC_DOC_TABS, TOP_LEFT, TOP_RIGHT);
    if (m_wndBrowser.GetSafeHwnd() != NULL)
        AddAnchor(m_wndBrowser, TOP_LEFT, BOTTOM_RIGHT);

    NavigateTo(m_currentPage);
    return TRUE;
}

void CDocViewerDlg::OnSize(UINT nType, int cx, int cy)
{
    CResizableDialog::OnSize(nType, cx, cy);
    // ResizableLib handles the anchor repositioning automatically
}

void CDocViewerDlg::NavigateTo(int page)
{
    if (page < 0 || page > 2) page = 0;
    m_currentPage = page;

    if (IsWindow(m_tabs.m_hWnd))
        m_tabs.SetCurSel(page);

    if (m_wndBrowser.GetSafeHwnd() == NULL) return;

    // Call IWebBrowser2::Navigate (DISPID 0x68).
    // VTS_BSTR: MFC converts LPCSTR to BSTR for the COM dispatch call.
    CString url(s_urls[page]);
    COleVariant varEmpty;
    static BYTE parms[] = VTS_BSTR VTS_PVARIANT VTS_PVARIANT VTS_PVARIANT VTS_PVARIANT;
    m_wndBrowser.InvokeHelper(
        0x68,                   // DISPID for Navigate
        DISPATCH_METHOD,
        VT_EMPTY, NULL,
        parms,
        (LPCTSTR)url,
        &varEmpty, &varEmpty, &varEmpty, &varEmpty);
}

void CDocViewerDlg::OnClose()
{
    DestroyWindow();
}

void CDocViewerDlg::PostNcDestroy()
{
    delete this;
}

void CDocViewerDlg::OnSelchangeTabs(NMHDR * /*pNMHDR*/, LRESULT *pResult)
{
    int sel = m_tabs.GetCurSel();
    if (sel >= 0)
        NavigateTo(sel);
    *pResult = 0;
}
