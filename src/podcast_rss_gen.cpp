// podcast_rss_gen.cpp — Podcast RSS 2.0 + iTunes feed generator (Phase 5)
//
// Pure C file I/O — no extra libraries beyond the C runtime.
//

#include "platform.h"       /* cross-platform shims — must come first */
#include "podcast_rss_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/* Resolve a text field: prefer 'primary', fall back to 'fallback', then 'dflt' */
static const char *resolve(const char *primary, const char *fallback, const char *dflt)
{
    if (primary && primary[0]) return primary;
    if (fallback && fallback[0]) return fallback;
    return dflt;
}

/* Return MIME type string for the audio file extension */
static const char *mimeForPath(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (mc_stricmp(dot, ".mp3")  == 0) return "audio/mpeg";
    if (mc_stricmp(dot, ".ogg")  == 0) return "audio/ogg";
    if (mc_stricmp(dot, ".opus") == 0) return "audio/ogg; codecs=opus";
    if (mc_stricmp(dot, ".flac") == 0) return "audio/flac";
    if (mc_stricmp(dot, ".aac")  == 0) return "audio/aac";
    if (mc_stricmp(dot, ".m4a")  == 0) return "audio/mp4";
    if (mc_stricmp(dot, ".wav")  == 0) return "audio/wav";
    return "application/octet-stream";
}

/* Get file size in bytes; returns 0 on error */
static long long fileSize(const char *path)
{
    mc_stat_t st;
    if (mc_stat(path, &st) == 0) return (long long)st.st_size;
    return 0;
}

/* Format seconds as HH:MM:SS */
static void formatDuration(long seconds, char *out, size_t outLen)
{
    long h = seconds / 3600;
    long m = (seconds % 3600) / 60;
    long s = seconds % 60;
    snprintf(out, outLen - 1, "%02ld:%02ld:%02ld", h, m, s);
    out[outLen - 1] = '\0';
}

/* Estimate duration from WAV bytes: bytes / (samplerate * channels * bytesPerSample) */
static long estimateWavDuration(long long bytes, int samplerate, int channels)
{
    if (samplerate <= 0 || channels <= 0) return 0;
    long denom = (long)samplerate * (long)channels * 2L; /* 16-bit = 2 bytes */
    if (denom <= 0) return 0;
    return (long)(bytes / denom);
}

/* RFC 2822 date string from current time */
static void rfc2822Now(char *out, size_t outLen)
{
    time_t t = time(NULL);
    struct tm *gmt = gmtime(&t);
    strftime(out, outLen, "%a, %d %b %Y %H:%M:%S +0000", gmt);
}

/* ISO 8601 date string (YYYY-MM-DD) from current time */
static void isoDateNow(char *out, size_t outLen)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    strftime(out, outLen, "%Y-%m-%d", lt);
}

/*
 * Minimal XML entity escaping for attribute and text content.
 * Writes into 'out' (null-terminated), truncating at outLen-1.
 */
static void xmlEscape(const char *src, char *out, size_t outLen)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 8 < outLen; i++) {
        switch (src[i]) {
        case '&':  memcpy(out + j, "&amp;",  5); j += 5; break;
        case '<':  memcpy(out + j, "&lt;",   4); j += 4; break;
        case '>':  memcpy(out + j, "&gt;",   4); j += 4; break;
        case '"':  memcpy(out + j, "&quot;", 6); j += 6; break;
        case '\'': memcpy(out + j, "&apos;", 6); j += 6; break;
        default:   out[j++] = src[i]; break;
        }
    }
    out[j] = '\0';
}

/* Build the RSS output path: same dir + basename + ".rss" */
static void buildRSSPath(const char *audioPath, char *out, size_t outLen)
{
    strncpy(out, audioPath, outLen - 1);
    out[outLen - 1] = '\0';
    /* Find the last '.' and replace extension */
    char *dot = strrchr(out, '.');
    if (dot) {
        strncpy(dot, ".rss", outLen - (dot - out) - 1);
    } else {
        strncat(out, ".rss", outLen - strlen(out) - 1);
    }
}

/* Extract just the filename from a full path */
static const char *baseName(const char *path)
{
    const char *sl = strrchr(path, '\\');
    if (!sl) sl = strrchr(path, '/');
    return sl ? sl + 1 : path;
}

// ---------------------------------------------------------------------------
// generate_podcast_rss
// ---------------------------------------------------------------------------

int generate_podcast_rss(const mcaster1Globals *g, const char *audio_path)
{
    if (!g || !audio_path || !audio_path[0]) return 0;

    /* Build RSS file path */
    char rssPath[1024];
    buildRSSPath(audio_path, rssPath, sizeof(rssPath));

    FILE *fp = fopen(rssPath, "w");
    if (!fp) return 0;

    /* --- Resolve metadata fields ----------------------------------------- */
    char esc[2048];  /* scratch buffer for XML-escaped strings */

    const char *title      = resolve(g->gPodcastTitle,    g->gServName, "Untitled Show");
    const char *link       = resolve(g->gPodcastWebsite,  g->gServURL,  "");
    const char *desc       = resolve(g->gPodcastDescription, g->gServDesc, title);
    const char *lang       = resolve(g->gPodcastLanguage,  "",           "en-us");
    const char *copyright_ = resolve(g->gPodcastCopyright, "",           "");
    const char *author     = resolve(g->gPodcastAuthor,   g->gServName, "Unknown Host");
    const char *category   = resolve(g->gPodcastCategory, g->gServGenre,"Podcast");
    const char *coverArt   = resolve(g->gPodcastCoverArt, "",           "");

    /* --- Date strings ------------------------------------------------------ */
    char rfcDate[64];
    char isoDate[32];
    rfc2822Now(rfcDate, sizeof(rfcDate));
    isoDateNow(isoDate, sizeof(isoDate));

    /* --- File info ---------------------------------------------------------- */
    long long fsize    = fileSize(audio_path);
    const char *mime   = mimeForPath(audio_path);
    const char *fname  = baseName(audio_path);

    /* Build episode title: "Show — YYYY-MM-DD" */
    char epTitle[512];
    snprintf(epTitle, sizeof(epTitle) - 1, "%s \xe2\x80\x94 %s", title, isoDate);
    epTitle[sizeof(epTitle) - 1] = '\0';

    /* Duration estimate (WAV only — encoded formats omit it) */
    char durStr[16] = "";
    const char *dot = strrchr(audio_path, '.');
    if (dot && mc_stricmp(dot, ".wav") == 0) {
        long secs = estimateWavDuration(
            g->written > 0 ? (long long)g->written : fsize,
            (int)(g->currentSamplerate > 0 ? g->currentSamplerate : 44100),
            g->currentChannels > 0 ? g->currentChannels : 2);
        if (secs > 0)
            formatDuration(secs, durStr, sizeof(durStr));
    }

    /* --- Build file:// URI for enclosure (local file) --------------------- */
    char enclosureURL[1280];
    /* Convert backslashes to forward slashes for URI */
    char uriPath[1024];
    strncpy(uriPath, audio_path, sizeof(uriPath) - 1);
    uriPath[sizeof(uriPath) - 1] = '\0';
    for (char *p = uriPath; *p; p++) { if (*p == '\\') *p = '/'; }
    snprintf(enclosureURL, sizeof(enclosureURL) - 1, "file:///%s", uriPath);

    /* --- Write RSS --------------------------------------------------------- */
    fprintf(fp,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<rss version=\"2.0\"\n"
        "     xmlns:itunes=\"http://www.itunes.com/dtds/podcast-1.0.dtd\"\n"
        "     xmlns:content=\"http://purl.org/rss/modules/content/\">\n"
        "  <channel>\n");

    xmlEscape(title, esc, sizeof(esc));
    fprintf(fp, "    <title>%s</title>\n", esc);

    xmlEscape(link, esc, sizeof(esc));
    fprintf(fp, "    <link>%s</link>\n", esc);

    xmlEscape(desc, esc, sizeof(esc));
    fprintf(fp, "    <description>%s</description>\n", esc);

    fprintf(fp, "    <language>%s</language>\n", lang);

    if (copyright_[0]) {
        xmlEscape(copyright_, esc, sizeof(esc));
        fprintf(fp, "    <copyright>%s</copyright>\n", esc);
    }

    fprintf(fp, "    <lastBuildDate>%s</lastBuildDate>\n", rfcDate);

    /* iTunes namespace elements */
    xmlEscape(author, esc, sizeof(esc));
    fprintf(fp, "    <itunes:author>%s</itunes:author>\n", esc);

    xmlEscape(category, esc, sizeof(esc));
    fprintf(fp, "    <itunes:category text=\"%s\"/>\n", esc);

    if (coverArt[0]) {
        xmlEscape(coverArt, esc, sizeof(esc));
        fprintf(fp, "    <itunes:image href=\"%s\"/>\n", esc);
    }

    fprintf(fp, "    <itunes:explicit>no</itunes:explicit>\n");

    /* Single episode item */
    fprintf(fp, "    <item>\n");

    xmlEscape(epTitle, esc, sizeof(esc));
    fprintf(fp, "      <title>%s</title>\n", esc);

    char itemDesc[512];
    snprintf(itemDesc, sizeof(itemDesc) - 1, "Live stream recording: %s", title);
    xmlEscape(itemDesc, esc, sizeof(esc));
    fprintf(fp, "      <description>%s</description>\n", esc);

    xmlEscape(enclosureURL, esc, sizeof(esc));
    fprintf(fp,
        "      <enclosure url=\"%s\"\n"
        "                 length=\"%lld\"\n"
        "                 type=\"%s\"/>\n",
        esc, fsize, mime);

    xmlEscape(fname, esc, sizeof(esc));
    fprintf(fp, "      <guid isPermaLink=\"false\">%s</guid>\n", esc);

    fprintf(fp, "      <pubDate>%s</pubDate>\n", rfcDate);

    if (durStr[0])
        fprintf(fp, "      <itunes:duration>%s</itunes:duration>\n", durStr);

    fprintf(fp, "      <itunes:explicit>no</itunes:explicit>\n");

    /* ICY 2.2 podcast metadata as custom elements */
    if (g->gICY22DJHandle[0]) {
        xmlEscape(g->gICY22DJHandle, esc, sizeof(esc));
        fprintf(fp, "      <itunes:author>%s</itunes:author>\n", esc);
    }

    fprintf(fp,
        "    </item>\n"
        "  </channel>\n"
        "</rss>\n");

    fclose(fp);
    return 1;
}
