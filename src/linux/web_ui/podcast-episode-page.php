<?php
/**
 * podcast-episode-page.php — Public Podcast Episode Page
 *
 * File:    src/linux/web_ui/podcast-episode-page.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   PC-5
 * Purpose: We serve individual episode pages. This file delegates to podcast-site.php
 *          with the episode_id parameter set, so the single-episode view is rendered.
 *          The C++ route /shows/{id}/episodes/{eid} forwards here with both params.
 *
 * URL patterns:
 *   /podcast-episode-page.php?show_id=N&episode_id=M   — direct
 *   /shows/{id}/episodes/{eid}                           — clean URL (C++ route)
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - Public page: no auth required
 */

/* We ensure both params are set, then include the main site page which handles
   single-episode rendering when episode_id is present */
$_GET['show_id']    = $_GET['show_id']    ?? '0';
$_GET['episode_id'] = $_GET['episode_id'] ?? '0';

require __DIR__ . '/podcast-site.php';
