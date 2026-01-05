// MCASTER1DSPENCODER.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "MCASTER1DSPENCODER.h"
#include "MainWindow.h"
#include "config_yaml.h"
//#include "Dummy.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//extern CMainWindow *mainWindow;
CMainWindow *mainWindow;
CWnd *myWin;

char    logPrefix[255] = "MCASTER1DSPENCODER";

/////////////////////////////////////////////////////////////////////////////
// CMCASTER1DSPENCODER

BEGIN_MESSAGE_MAP(CMcaster1DSPEncoderApp, CWinApp)
	//{{AFX_MSG_MAP(CMcaster1DSPEncoderApp)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG
	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMcaster1DSPEncoderApp construction

void inputMetadataCallback(void *gbl, void *pValue) {
    mcaster1Globals *g = (mcaster1Globals *)gbl;
    mainWindow->inputMetadataCallback(g->encoderNumber, pValue);
}
void outputStatusCallback(void *gbl, void *pValue) {
    mcaster1Globals *g = (mcaster1Globals *)gbl;
    mainWindow->outputStatusCallback(g->encoderNumber, pValue);
}
void writeBytesCallback(void *gbl, void *pValue) {
    mcaster1Globals *g = (mcaster1Globals *)gbl;
    mainWindow->writeBytesCallback(g->encoderNumber, pValue);
}
void outputServerNameCallback(void *gbl, void *pValue) {
    mcaster1Globals *g = (mcaster1Globals *)gbl;
    mainWindow->outputServerNameCallback(g->encoderNumber, pValue);
}
void outputBitrateCallback(void *gbl, void *pValue) {
    mcaster1Globals *g = (mcaster1Globals *)gbl;
    mainWindow->outputBitrateCallback(g->encoderNumber, pValue);
}
void outputStreamURLCallback(void *gbl, void *pValue) {
    mcaster1Globals *g = (mcaster1Globals *)gbl;
    mainWindow->outputStreamURLCallback(g->encoderNumber, pValue);
}

int mcaster1_init(mcaster1Globals *g)
{
	int	printConfig = 0;
	


	setServerStatusCallback(g, outputStatusCallback);
	setGeneralStatusCallback(g, NULL);
	setWriteBytesCallback(g, writeBytesCallback);
	setBitrateCallback(g, outputBitrateCallback);
	setServerNameCallback(g, outputServerNameCallback);
	setDestURLCallback(g, outputStreamURLCallback);
    //strcpy(g->gConfigFileName, ".\\mcaster1dspencoder");

	/* Try YAML first.  If no YAML file exists yet, fall back to the legacy
	 * INI config file, then immediately write a YAML file for future runs
	 * (one-time automatic migration from .cfg to .yaml). */
	if (!readConfigYAML(g)) {
		char cfgPath[1024], bakPath[1024];
		readConfigFile(g, 1);     /* legacy INI read — readOnly, don't recreate .cfg */
		writeConfigYAML(g);       /* create YAML file */
		/* Rename legacy .cfg to .cfg.bak so we know migration is done */
		if (strlen(g->gConfigFileName) == 0) {
			_snprintf(cfgPath, sizeof(cfgPath)-1, "Mcaster1 DSP Encoder_%d.cfg", g->encoderNumber);
			_snprintf(bakPath, sizeof(bakPath)-1, "Mcaster1 DSP Encoder_%d.cfg.bak", g->encoderNumber);
		} else {
			_snprintf(cfgPath, sizeof(cfgPath)-1, "%s_%d.cfg", g->gConfigFileName, g->encoderNumber);
			_snprintf(bakPath, sizeof(bakPath)-1, "%s_%d.cfg.bak", g->gConfigFileName, g->encoderNumber);
		}
		cfgPath[sizeof(cfgPath)-1] = bakPath[sizeof(bakPath)-1] = '\0';
		MoveFileA(cfgPath, bakPath);
	}

	setFrontEndType(g, FRONT_END_MCASTER1_PLUGIN);
	
	return 1;
}


CMcaster1DSPEncoderApp::CMcaster1DSPEncoderApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CMcaster1DSPEncoderApp object

CMcaster1DSPEncoderApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CMcaster1DSPEncoderApp initialization
/*
bool done;
INT  nResult;

int RunModalWindow(HWND hwndDialog,HWND hwndParent)
{
  if(hwndParent != NULL)
    EnableWindow(hwndParent,FALSE);

  MSG msg;
  
  for(done=false;done==false;WaitMessage())
  {
    while(PeekMessage(&msg,0,0,0,PM_REMOVE))
    {
      if(msg.message == WM_QUIT)
      {
        done = true;
        PostMessage(NULL,WM_QUIT,0,0);
        break;
      }

      if(!IsDialogMessage(hwndDialog,&msg))
      {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }
  }

  if(hwndParent != NULL)
    EnableWindow(hwndParent,TRUE);

  DestroyWindow(hwndDialog);

  return nResult;
}
*/

void CMcaster1DSPEncoderApp::SetMainAfxWin(CWnd *pwnd) {
    m_pMainWnd = pwnd;
}

BOOL CMcaster1DSPEncoderApp::InitInstance()
{
	AfxEnableControlContainer();

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	//  of your final executable, you should remove from the following
	//  the specific initialization routines you do not need.

#ifdef _AFXDLL
	Enable3dControls();			// Call this when using MFC in a shared DLL
#else
	Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif

	char currentDir[MAX_PATH] = ".";
	char tmpfile[MAX_PATH] = "";
	sprintf(tmpfile, "%s\\.tmp", currentDir);

	FILE *filep = fopen(tmpfile, "w");
	if (filep == 0) {
		char path[MAX_PATH] = "";

		SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path);
		strcpy(currentDir, path);
	}
	else {
		fclose(filep);
	}

    LoadConfigs(currentDir, "MCASTER1DSPENCODER");

    mainWindow = new CMainWindow(m_pMainWnd);

    theApp.SetMainAfxWin(mainWindow);

    mainWindow->InitializeWindow();

    
    //mainWindow->Create((UINT)IDD_MCASTER1, this);
	mainWindow->DoModal();

	return FALSE;
}
