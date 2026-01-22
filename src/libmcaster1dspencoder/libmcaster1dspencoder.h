#ifndef __DSP_MCASTER1_H
#define __DSP_MCASTER1_H

#include <pthread.h>

#include "cbuffer.h"

#include "libmcaster1dspencoder_socket.h"
#ifdef HAVE_VORBIS
#include <vorbis/vorbisenc.h>
#endif

#include <stdio.h>
#include <time.h>

#ifdef _DMALLOC_
#include <dmalloc.h>
#endif

/*
#ifdef _UNICODE
#define char_t wchar_t
#define atoi _wtoi
#define LPCSTR LPCWSTR 
#define strcpy wcscpy
#define strcmp wcscmp
#define strlen wcslen
#define fopen _wfopen
#define strstr wcsstr
#define sprintf swprintf
#define strcat wcscat
#define fgets fgetws
#else
#define char_t char
#endif
*/
#define char_t char

#ifdef __cplusplus
extern "C" {
#endif
#include "libmcaster1dspencoder_resample.h"
#ifdef __cplusplus
}
#endif

#ifdef WIN32
#include <lame/lame.h>
#include <fdk-aac/aacenc_lib.h>
#include <opus/opusenc.h>
#else
#ifdef HAVE_LAME
#include <lame/lame.h>
#endif
#endif

#ifdef HAVE_AACP
#include "enc_if.h"
typedef AudioCoder* (*CREATEAUDIO3TYPE)(int , int , int , unsigned int , unsigned int *, char_t *);
typedef unsigned int (*GETAUDIOTYPES3TYPE)(int, char_t *);
#endif

#define LM_FORCE 0
#define LM_ERROR 1
#define LM_INFO 2
#define LM_DEBUG 3
#ifdef WIN32
#define LOG_FORCE LM_FORCE, TEXT(__FILE__), __LINE__
#define LOG_ERROR LM_ERROR, TEXT(__FILE__), __LINE__
#define LOG_INFO LM_INFO, TEXT(__FILE__), __LINE__
#define LOG_DEBUG LM_DEBUG, TEXT(__FILE__), __LINE__
#else
#define LOG_FORCE LM_FORCE, __FILE__, __LINE__
#define LOG_ERROR LM_ERROR, __FILE__, __LINE__
#define LOG_INFO LM_INFO, __FILE__, __LINE__
#define LOG_DEBUG LM_DEBUG, __FILE__, __LINE__
#endif


#ifdef HAVE_FLAC
#include <FLAC/stream_encoder.h>
extern "C" {
#include <FLAC/metadata.h>
}
#endif

#define FormatID 'fmt '   /* chunkID for Format Chunk. NOTE: There is a space at the end of this ID. */
// For skin stuff
#define WINDOW_WIDTH		276
#define WINDOW_HEIGHT		150

#ifndef FALSE
#define FALSE false
#endif
#ifndef TRUE
#define TRUE true
#endif
#ifndef WIN32
#include <sys/ioctl.h>
#else
#include <mmsystem.h>
#endif
// Callbacks
#define	BYTES_PER_SECOND 1

#define FRONT_END_MCASTER1_PLUGIN 1
#define FRONT_END_TRANSCODER 2

typedef struct tagLAMEOptions {
	int		cbrflag;
	int		out_samplerate;
	int		quality;
#ifdef WIN32
	int		mode;
#else
#ifdef HAVE_LAME
	MPEG_mode	mode;
#endif
#endif
	int		brate;
	int		copywrite;
	int		original;
	int		strict_ISO;
	int		disable_reservoir;
	char_t		VBR_mode[25];
	int		VBR_mean_bitrate_kbps;
	int		VBR_min_bitrate_kbps;
	int		VBR_max_bitrate_kbps;
	int		lowpassfreq;
	int		highpassfreq;
} LAMEOptions;

typedef struct {
	char_t	RIFF[4];
	long	chunkSize;
	char_t	WAVE[4];
} RIFFChunk;

typedef struct {
  char_t		chunkID[4];
  long		chunkSize;

  short          wFormatTag;
  unsigned short wChannels;
  unsigned long  dwSamplesPerSec;
  unsigned long  dwAvgBytesPerSec;
  unsigned short wBlockAlign;
  unsigned short wBitsPerSample;

/* Note: there may be additional fields here, depending upon wFormatTag. */

} FormatChunk;


typedef struct {
	char_t	chunkID[4];
	long	chunkSize;
	short *	waveformData;
} DataChunk;

struct wavhead
{
	unsigned int  main_chunk;
	unsigned int  length;
	unsigned int  chunk_type;
	unsigned int  sub_chunk;
	unsigned int  sc_len;
	unsigned short  format;
	unsigned short  modus;
	unsigned int  sample_fq;
	unsigned int  byte_p_sec;
	unsigned short  byte_p_spl;
	unsigned short  bit_p_spl;
	unsigned int  data_chunk;
	unsigned int  data_length;
};


static struct wavhead   wav_header;

// Global variables....gotta love em...
typedef struct {
	long		currentSamplerate;
	int		currentBitrate;
	int		currentBitrateMin;
	int		currentBitrateMax;
	int		currentChannels;
	int		gSCSocket;
	int		gSCSocket2;
	int		gSCSocketControl;
	CMySocket	dataChannel;
	CMySocket	controlChannel;
	int		gSCFlag;
	int		gCountdown;
	int		gAutoCountdown;
	int		automaticconnect;
	char_t		gSourceURL[1024];
	char_t		gServer[256];
	char_t		gPort[10];
	char_t		gPassword[256];
	int		weareconnected;
	char_t		gIniFile[1024];
	char_t		gAppName[256];
	char_t		gCurrentSong[1024];
	int			gSongUpdateCounter;
	char_t		gMetadataUpdate[10];
	int			gPubServ;
	char_t		gServIRC[20];
	char_t		gServICQ[20];
	char_t		gServAIM[20];
	char_t		gServURL[1024];
	char_t		gServDesc[1024];
	char_t		gServName[1024];
	char_t		gServGenre[100];
	char_t		gMountpoint[100];
	char_t		gFrequency[10];
	char_t		gChannels[10];
	int			gAutoReconnect;
	int 		gReconnectSec;
	char_t		gAutoStart[10];
	char_t		gAutoStartSec[20];
	char_t		gQuality[5];
#ifndef WIN32
#ifdef HAVE_LAME
	lame_global_flags *gf;
#endif
#endif
#ifdef WIN32
	// LAME built-in MP3
	lame_global_flags *lameGF;
	int lameVBRMode;       // 0=CBR, 1=VBR, 2=ABR
	int lameVBRQuality;    // V0-V9 for VBR mode
	int lameABRMean;       // target kbps for ABR
	int lameMinBitrate;    // min kbps (VBR/ABR)
	int lameMaxBitrate;    // max kbps (VBR/ABR)

	// fdk-aac (AAC-LC, AAC+, AAC++)
	HANDLE_AACENCODER fdkAacEncoder;
	int fdkAacProfile;     // 2=AAC-LC, 5=HE-AAC(AAC+), 29=HE-AAC v2(AAC++)

	// Opus
	OggOpusEnc *opusEncoder;
	OggOpusComments *opusComments;
	int opusComplexity;    // 0-10, default 10

	// New format flags
	int gOpusFlag;
	int gAACLCFlag;     // AAC-LC via fdk-aac
	int gAACPlusFlag;   // HE-AAC v1 (AAC+) via fdk-aac
	int gAAC2Flag;      // HE-AAC v2 (AAC++) via fdk-aac
#endif
	int		gCurrentlyEncoding;
	int		gFLACFlag;
	int		gAACFlag;
	int		gAACPFlag;
	int		gOggFlag;
	char_t		gIceFlag[10];
	int		gLAMEFlag;
	char_t		gOggQuality[25];
	int			gLiveRecordingFlag;
	int		gOggBitQualFlag;
	char_t	gOggBitQual[40];
	char_t	gEncodeType[25];
	int		gAdvancedRecording;
	int		gNOggchannels;
	char_t		gModes[4][255];
	int		gShoutcastFlag;
	int		gIcecastFlag;
	int		gIcecast2Flag;
	char_t		gSaveDirectory[1024];
	char_t		gLogFile[1024];
	int			gLogLevel;
	FILE		*logFilep;
	int		gSaveDirectoryFlag;
	int		gSaveAsWAV;
	FILE		*gSaveFile;
	LAMEOptions	gLAMEOptions;
	int		gLAMEHighpassFlag;
	int		gLAMELowpassFlag;

	int		oggflag;
	int		serialno;
#ifdef HAVE_VORBIS
	ogg_sync_state  oy_stream;
	ogg_packet header_main_save;
	ogg_packet header_comments_save;
	ogg_packet header_codebooks_save;
#endif
	bool		ice2songChange;
	int			in_header;
	long		 written;
	int		vuShow;

	int		gLAMEpreset;
	char_t	gLAMEbasicpreset[255];
	char_t	gLAMEaltpreset[255];
	char_t    gSongTitle[1024];
	char_t    gManualSongTitle[1024];
	int		gLockSongTitle;
    int     gNumEncoders;

	mc1_res_state	resampler;
	int	initializedResampler;
	void (*sourceURLCallback)(void *, void *);
	void (*destURLCallback)(void *, void *);
	void (*serverStatusCallback)(void *, void *);
	void (*generalStatusCallback)(void *, void *);
	void (*writeBytesCallback)(void *, void *);
	void (*serverTypeCallback)(void *, void *);
	void (*serverNameCallback)(void *, void *);
	void (*streamTypeCallback)(void *, void *);
	void (*bitrateCallback)(void *, void *);
	void (*VUCallback)(int, int);
	long	startTime;
	long	endTime;
	char_t	sourceDescription[255];

	char_t	gServerType[25];
#ifdef WIN32
	WAVEFORMATEX waveFormat;
	HWAVEIN      inHandle;
	WAVEHDR                 WAVbuffer1;
	WAVEHDR                 WAVbuffer2;
#else
	int	inHandle; // for advanced recording
#endif
	unsigned long result;
	short int WAVsamplesbuffer1[1152*2];
	short int WAVsamplesbuffer2[1152*2];
	bool  areLiveRecording;
	char_t	gAdvRecDevice[255];
#ifndef WIN32
	char_t	gAdvRecServerTitle[255];
#endif
	int		gLiveInSamplerate;
#ifdef WIN32
        HINSTANCE       hAACPDLL;
#endif
		char_t	gConfigFileName[255];
		char_t	gOggEncoderText[255];
		int		gForceStop;
		char_t	gCurrentRecordingName[1024];
		long	lastX;
		long	lastY;
		long	lastDummyX;
		long	lastDummyY;

#ifdef HAVE_VORBIS
		ogg_stream_state os;
		vorbis_dsp_state vd;
		vorbis_block     vb;
		vorbis_info      vi;
#endif

		int	frontEndType;
		int	ReconnectTrigger;
#ifdef HAVE_AACP
		CREATEAUDIO3TYPE	CreateAudio3;
		GETAUDIOTYPES3TYPE	GetAudioTypes3;

		AudioCoder *(*finishAudio3)(char_t *fn, AudioCoder *c);
		void (*PrepareToFinish)(const char_t *filename, AudioCoder *coder);
		AudioCoder * aacpEncoder;
#endif
		char_t		gAACQuality[25];
		char_t		gAACCutoff[25];
        int     encoderNumber;
        bool    forcedDisconnect;
        time_t     forcedDisconnectSecs;
        int    autoconnect;
        char_t    externalMetadata[255];
        char_t    externalURL[255];
        char_t    externalFile[255];
        char_t    externalInterval[25];
        char_t    *vorbisComments[30];
        int     numVorbisComments;
        char_t    outputControl[255];
        char_t    metadataAppendString[255];
        char_t    metadataRemoveStringBefore[255];
        char_t    metadataRemoveStringAfter[255];
        char_t    metadataWindowClass[255];
        bool    metadataWindowClassInd;
#ifdef HAVE_FLAC
		FLAC__StreamEncoder	*flacEncoder;
		FLAC__StreamMetadata *flacMetadata;
		int	flacFailure;
#endif
		char_t	*configVariables[255];
		int		numConfigVariables;
		pthread_mutex_t mutex;

		char_t	WindowsRecDevice[255];

		int		LAMEJointStereoFlag;
		CBUFFER	circularBuffer;

	/* ---- Phase 5: Podcast RSS generation ---- */
	int     gGenerateRSS;               /* 0/1 — write .rss alongside saved file */
	int     gRSSUseYPSettings;          /* 0/1 — seed RSS fields from YP settings */
	char_t  gPodcastTitle[256];         /* podcast show title */
	char_t  gPodcastAuthor[256];        /* host / author name */
	char_t  gPodcastCategory[128];      /* iTunes category e.g. "Technology" */
	char_t  gPodcastLanguage[16];       /* ISO 639-1 e.g. "en-us" */
	char_t  gPodcastCopyright[256];     /* copyright string */
	char_t  gPodcastWebsite[1024];      /* podcast homepage URL */
	char_t  gPodcastCoverArt[1024];     /* cover art image URL */
	char_t  gPodcastDescription[2048];  /* show description / summary */
	char_t  gLastSavedFilePath[1024];   /* set by openArchiveFile, consumed by RSS gen */
	/* Podcast episode-level fields (icy-meta-podcast-*) */
	char_t  gICY22PodcastEpisode[256];  /* icy-meta-podcast-episode — episode title/ID  */
	char_t  gICY22PodcastRating[32];    /* icy-meta-podcast-rating  — all-ages|teen|mature|explicit */
	char_t  gICY22PodcastRSSURL[1024];  /* icy-meta-podcast-rss     — feed URL          */
	int     gICY22PodcastDuration;      /* icy-meta-duration        — runtime in seconds */

	/* ---- Phase 5: ICY 2.2 Extended metadata ---- */
	/* Station Identity */
	char_t  gICY22StationID[256];
	char_t  gICY22StationLogo[1024];
	char_t  gICY22VerifyStatus[32];     /* unverified|pending|verified|gold */
	/* Station Certificate / PKI (icy-meta-cert*, icy-meta-ssh-pubkey) */
	char_t  gICY22CertIssuerID[128];    /* icy-meta-certissuer-id           */
	char_t  gICY22CertRootCA[256];      /* icy-meta-cert-rootca  — CA hash  */
	char_t  gICY22Certificate[4096];    /* icy-meta-certificate  — Base64 PEM cert */
	char_t  gICY22SSHPubKey[1024];      /* icy-meta-ssh-pubkey              */
	/* Show / Programming */
	char_t  gICY22ShowTitle[256];
	char_t  gICY22ShowStart[64];        /* ISO8601 */
	char_t  gICY22ShowEnd[64];
	char_t  gICY22NextShow[256];
	char_t  gICY22NextShowTime[64];
	char_t  gICY22ScheduleURL[1024];
	int     gICY22AutoDJ;
	char_t  gICY22PlaylistName[256];
	/* DJ / Host */
	char_t  gICY22DJHandle[128];
	char_t  gICY22DJBio[512];           /* max 280 chars per spec */
	char_t  gICY22DJGenre[256];         /* comma-separated, max 5 */
	char_t  gICY22DJRating[32];
	/* Social & Discovery */
	char_t  gICY22CreatorHandle[128];
	char_t  gICY22SocialTwitter[128];
	char_t  gICY22SocialTwitch[128];
	char_t  gICY22SocialIG[128];
	char_t  gICY22SocialTikTok[128];
	char_t  gICY22SocialYouTube[256];
	char_t  gICY22SocialFacebook[256];
	char_t  gICY22SocialLinkedIn[256];
	char_t  gICY22SocialLinktree[1024];
	char_t  gICY22Emoji[16];
	char_t  gICY22Hashtags[512];        /* JSON array e.g. ["#rock","#live"] */
	/* Listener Engagement */
	int     gICY22RequestEnabled;
	char_t  gICY22RequestURL[1024];
	char_t  gICY22ChatURL[1024];
	char_t  gICY22TipURL[1024];
	char_t  gICY22EventsURL[1024];
	/* Broadcast Distribution */
	char_t  gICY22CrosspostPlatforms[512]; /* comma-separated active platforms */
	char_t  gICY22SessionID[256];          /* UUID auto-gen on connect if blank */
	char_t  gICY22CDNRegion[64];
	char_t  gICY22RelayOrigin[1024];
	/* Compliance / Content Flags */
	int     gICY22NSFW;
	int     gICY22AIGenerator;
	char_t  gICY22GeoRegion[64];
	char_t  gICY22LicenseType[64];      /* cc-by|cc-by-sa|cc0|pro-licensed|all-rights-reserved */
	int     gICY22RoyaltyFree;
	char_t  gICY22LicenseTerritory[256];/* ISO country codes or GLOBAL */
	/* Station Notice */
	char_t  gICY22NoticeText[512];
	char_t  gICY22NoticeURL[1024];
	char_t  gICY22NoticeExpires[64];    /* ISO8601 */
	/* Video / Simulcast */
	char_t  gICY22VideoType[32];        /* live|short|clip|trailer|ad */
	char_t  gICY22VideoLink[1024];
	char_t  gICY22VideoTitle[256];
	char_t  gICY22VideoPoster[1024];
	char_t  gICY22VideoPlatform[32];    /* youtube|tiktok|twitch|kick|rumble|vimeo|custom */
	int     gICY22VideoLive;
	char_t  gICY22VideoCodec[32];
	char_t  gICY22VideoFPS[16];
	char_t  gICY22VideoResolution[32];  /* e.g. "1920x1080" */
	char_t  gICY22VideoRating[16];
	int     gICY22VideoNSFW;
	char_t  gICY22VideoChannel[128];    /* icy-meta-videochannel — creator/channel handle  */
	char_t  gICY22VideoStart[64];       /* icy-meta-videostart   — ISO8601 scheduled start */
	/* Audio Technical */
	char_t  gICY22LoudnessLUFS[16];    /* EBU R128 e.g. "-14.0" — icy-meta-loudness       */
	/* Auth (icy-meta-auth-token) */
	char_t  gICY22AuthToken[1024];      /* JWT bearer or access token                      */
	/* Track Metadata (icy-meta-track-*) — updated per-track in playlist/file-decode mode  */
	char_t  gICY22TrackArtwork[1024];   /* icy-meta-track-artwork  — album art URL         */
	char_t  gICY22TrackAlbum[256];      /* icy-meta-track-album    — release/album name    */
	char_t  gICY22TrackYear[8];         /* icy-meta-track-year     — YYYY                  */
	char_t  gICY22TrackLabel[256];      /* icy-meta-track-label    — record label           */
	char_t  gICY22TrackBPM[8];          /* icy-meta-track-bpm      — beats per minute       */
	char_t  gICY22TrackKey[16];         /* icy-meta-track-key      — Camelot or std notation*/
	char_t  gICY22TrackGenre[128];      /* icy-meta-track-genre    — per-track genre        */
	char_t  gICY22TrackMBID[64];        /* icy-meta-track-mbid     — MusicBrainz UUID       */
	char_t  gICY22TrackISRC[32];        /* icy-meta-track-isrc     — ISRC code              */
} mcaster1Globals;


/* ── HTTP Admin Server Configuration ────────────────────────────────────────
 *
 * Parsed from the top-level  http-admin:  block in mcaster1dspencoder.yaml.
 * Independent of per-encoder mcaster1Globals — one instance per process.
 *
 * YAML shape:
 *   http-admin:
 *     enabled: true
 *     listen-sockets:
 *       - port: 8330
 *         bind-address: "127.0.0.1"
 *         ssl: false
 *       - port: 8443
 *         bind-address: "0.0.0.0"
 *         ssl: true
 *         ssl-cert: "/etc/mcaster1/certs/server.crt"   # overrides ssl-config
 *         ssl-key:  "/etc/mcaster1/certs/server.key"
 *     auth:
 *       username:        "admin"
 *       password:        "hackme"
 *       api-token:       ""
 *       htpasswd-file:   ""
 *       session-timeout: 3600
 *     ssl-config:
 *       cert:     "/etc/mcaster1/certs/server.crt"
 *       key:      "/etc/mcaster1/certs/server.key"
 *       ca-chain: ""
 * ─────────────────────────────────────────────────────────────────────────── */

#define MC1_MAX_LISTENERS 8

typedef struct {
	char bind_address[64];   /* "127.0.0.1", "0.0.0.0", "::", etc.           */
	int  port;               /* TCP port number                               */
	int  ssl_enabled;        /* 0 = plain HTTP, 1 = HTTPS                    */
	char ssl_cert[1024];     /* per-socket cert path; empty = use ssl-config  */
	char ssl_key[1024];      /* per-socket key path;  empty = use ssl-config  */
} mc1ListenSocket;

typedef struct {
	int             enabled;                        /* 0/1 — run admin server  */

	/* Listener sockets */
	mc1ListenSocket sockets[MC1_MAX_LISTENERS];
	int             num_sockets;

	/* Authentication */
	char            admin_username[128];
	char            admin_password[256];            /* plaintext or sha256:<hex>    */
	char            admin_api_token[256];           /* X-API-Token header for scripts*/
	char            admin_htpasswd_file[1024];      /* Apache htpasswd path; if set  */
	                                                /* overrides username/password   */
	int             session_timeout_secs;           /* cookie session TTL (default 3600) */

	/* Default SSL certificate (applies to every ssl-enabled listener
	 * unless the listener has its own ssl-cert/ssl-key set)          */
	char            ssl_cert[1024];
	char            ssl_key[1024];
	char            ssl_ca[1024];                   /* optional CA chain bundle      */
} mc1AdminConfig;

/* Single global instance — defined in config_yaml.cpp,
 * populated by readAdminConfig() during startup.          */
#ifdef __cplusplus
extern "C" {
#endif
extern mc1AdminConfig gAdminConfig;
#ifdef __cplusplus
}
#endif

/* ─────────────────────────────────────────────────────────────────────────── */

void addConfigVariable(mcaster1Globals *g, char_t *variable);
int initializeencoder(mcaster1Globals *g);
void getCurrentSongTitle(mcaster1Globals *g, char_t *song, char_t *artist, char_t *full);
void initializeGlobals(mcaster1Globals *g);
void ReplaceString(char_t *source, char_t *dest, char_t *from, char_t *to);
void config_read(mcaster1Globals *g);
void config_write(mcaster1Globals *g);
int connectToServer(mcaster1Globals *g);
int disconnectFromServer(mcaster1Globals *g);
int do_encoding(mcaster1Globals *g, short int *samples, int numsamples, int nch);
void URLize(char_t *input, char_t *output, int inputlen, int outputlen);
int updateSongTitle(mcaster1Globals *g, int forceURL);
int setCurrentSongTitleURL(mcaster1Globals *g, char_t *song);
void icecast2SendMetadata(mcaster1Globals *g);
int ogg_encode_dataout(mcaster1Globals *g);
int	trimVariable(char_t *variable);
int readConfigFile(mcaster1Globals *g,int readOnly = 0);
int writeConfigFile(mcaster1Globals *g);
/* Helpers for YAML config layer — reset/populate the internal config store */
void configReset(void);
void configAddKeyValue(const char *key, const char *value);
void    printConfigFileValues();
void ErrorMessage(char_t *title, char_t *fmt, ...);
int setCurrentSongTitle(mcaster1Globals *g,char_t *song);
char_t*   getSourceURL(mcaster1Globals *g);
void    setSourceURL(mcaster1Globals *g,char_t *url);
long    getCurrentSamplerate(mcaster1Globals *g);
int     getCurrentBitrate(mcaster1Globals *g);
int     getCurrentChannels(mcaster1Globals *g);
int  ocConvertAudio(mcaster1Globals *g,float *in_samples, float *out_samples, int num_in_samples, int num_out_samples);
int initializeResampler(mcaster1Globals *g,long inSampleRate, long inNCH);
int handle_output(mcaster1Globals *g, float *samples, int nsamples, int nchannels, int in_samplerate);
void setServerStatusCallback(mcaster1Globals *g,void (*pCallback)(void *,void *));
void setGeneralStatusCallback(mcaster1Globals *g, void (*pCallback)(void *,void *));
void setWriteBytesCallback(mcaster1Globals *g, void (*pCallback)(void *,void *));
void setServerTypeCallback(mcaster1Globals *g, void (*pCallback)(void *,void *));
void setServerNameCallback(mcaster1Globals *g, void (*pCallback)(void *,void *));
void setStreamTypeCallback(mcaster1Globals *g, void (*pCallback)(void *,void *));
void setBitrateCallback(mcaster1Globals *g, void (*pCallback)(void *,void *));
void setVUCallback(mcaster1Globals *g, void (*pCallback)(int, int));
void setSourceURLCallback(mcaster1Globals *g, void (*pCallback)(void *,void *));
void setDestURLCallback(mcaster1Globals *g, void (*pCallback)(void *,void *));
void setSourceDescription(mcaster1Globals *g, char_t *desc);
int  getOggFlag(mcaster1Globals *g);
bool  getLiveRecordingFlag(mcaster1Globals *g);
void setLiveRecordingFlag(mcaster1Globals *g, bool flag);
void setDumpData(mcaster1Globals *g, int dump);
void setConfigFileName(mcaster1Globals *g, char_t *configFile);
char_t *getConfigFileName(mcaster1Globals *g);
char_t*	getServerDesc(mcaster1Globals *g);
int	getReconnectFlag(mcaster1Globals *g);
int getReconnectSecs(mcaster1Globals *g);
int getIsConnected(mcaster1Globals *g);
int resetResampler(mcaster1Globals *g);
void setOggEncoderText(mcaster1Globals *g, char_t *text);
int getLiveRecordingSetFlag(mcaster1Globals *g);
char_t *getCurrentRecordingName(mcaster1Globals *g);
void setCurrentRecordingName(mcaster1Globals *g, char_t *name);
void setForceStop(mcaster1Globals *g, int forceStop);
long	getLastXWindow(mcaster1Globals *g);
long	getLastYWindow(mcaster1Globals *g);
void	setLastXWindow(mcaster1Globals *g, long x);
void	setLastYWindow(mcaster1Globals *g, long y);
long	getLastDummyXWindow(mcaster1Globals *g);
long	getLastDummyYWindow(mcaster1Globals *g);
void	setLastDummyXWindow(mcaster1Globals *g, long x);
void	setLastDummyYWindow(mcaster1Globals *g, long y);
long	getVUShow(mcaster1Globals *g);
void	setVUShow(mcaster1Globals *g, long x);
int		getFrontEndType(mcaster1Globals *g);
void	setFrontEndType(mcaster1Globals *g, int x);
int		getReconnectTrigger(mcaster1Globals *g);
void	setReconnectTrigger(mcaster1Globals *g, int x);
char_t *getCurrentlyPlaying(mcaster1Globals *g);
long GetConfigVariableLong(char_t *appName, char_t *paramName, long defaultvalue, char_t *desc);
char_t	*getLockedMetadata(mcaster1Globals *g);
void	setLockedMetadata(mcaster1Globals *g, char_t *buf);
int		getLockedMetadataFlag(mcaster1Globals *g);
void	setLockedMetadataFlag(mcaster1Globals *g, int flag);
void	setSaveDirectory(mcaster1Globals *g, char_t *saveDir);
char_t *getSaveDirectory(mcaster1Globals *g);
char_t *getgLogFile(mcaster1Globals *g);
void	setgLogFile(mcaster1Globals *g,char_t *logFile);
int getSaveAsWAV(mcaster1Globals *g);
void setSaveAsWAV(mcaster1Globals *g, int flag);
FILE *getSaveFileP(mcaster1Globals *g);
long getWritten(mcaster1Globals *g);
void setWritten(mcaster1Globals *g, long writ);
int deleteConfigFile(mcaster1Globals *g);
void	setAutoConnect(mcaster1Globals *g, int flag);
void addVorbisComment(mcaster1Globals *g, char_t *comment);
void freeVorbisComments(mcaster1Globals *g);
void addBasicEncoderSettings(mcaster1Globals *g);
void addUISettings(mcaster1Globals *g);
void setDefaultLogFileName(char_t *filename);
void LogMessage(mcaster1Globals *g, int type, char_t *source, int line, char_t *fmt, ...);
char_t *getWindowsRecordingDevice(mcaster1Globals *g);
void	setWindowsRecordingDevice(mcaster1Globals *g, char_t *device);
int getLAMEJointStereoFlag(mcaster1Globals *g);
void	setLAMEJointStereoFlag(mcaster1Globals *g, int flag);
int triggerDisconnect(mcaster1Globals *g);
#endif
