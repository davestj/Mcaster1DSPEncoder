// podcast_rss_gen.h — Podcast RSS 2.0 + iTunes feed generator (Phase 5)
//
// Generates an RSS 2.0 feed with iTunes namespace alongside a saved
// archive recording.  The RSS file shares the same base name as the
// audio file with a .rss extension.
//
// Spec references:
//   RSS 2.0      : https://www.rssboard.org/rss-specification
//   iTunes NS    : https://podcasters.apple.com/support/823-podcast-requirements
//   ICY 2.2      : https://mcaster1.com/icy2-spec/index.html
//
// Usage:
//   After closeArchiveFile() writes and closes the audio recording, call:
//
//     if (g->gGenerateRSS && g->gLastSavedFilePath[0])
//         generate_podcast_rss(g, g->gLastSavedFilePath);
//

#pragma once
#ifndef PODCAST_RSS_GEN_H
#define PODCAST_RSS_GEN_H

#include "libmcaster1dspencoder/libmcaster1dspencoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * generate_podcast_rss()
 *
 *   g          — encoder globals containing podcast + YP metadata
 *   audio_path — absolute path to the saved audio file
 *
 * Writes {audio_basename}.rss in the same directory as audio_path.
 * Returns 1 on success, 0 on error.
 *
 * Field resolution order (first non-empty wins):
 *   Podcast Settings fields (gPodcastTitle, gPodcastAuthor, etc.)
 *   → YP Settings fields (gServName, gServDesc, gServURL, gServGenre)
 *   → safe defaults ("Untitled Show", "Unknown Host", etc.)
 */
int generate_podcast_rss(const mcaster1Globals *g, const char *audio_path);

#ifdef __cplusplus
}
#endif
#endif /* PODCAST_RSS_GEN_H */
