<?php
/**
 * icons.php — Server Type and Codec Format Badge Helpers
 *
 * File:    src/linux/web_ui/app/inc/icons.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-23
 * Purpose: We provide PHP badge helpers for server types and codec formats.
 *          We are included by any page that displays server type or codec info.
 *          Requires h() from db.php to be loaded first.
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural throughout all comments
 *
 * CHANGELOG:
 *  2026-02-23  v1.0.0  Initial implementation — server + codec badge helpers + CSS
 */

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
    return;
}

/* ── Server type badge definitions ──────────────────────────────────────────
 * We define labels, abbreviations, and CSS class names for every server type
 * supported by the streaming_servers table enum.
 * ─────────────────────────────────────────────────────────────────────────── */
define('MC1_SERVER_TYPES', [
    'icecast2'     => ['label' => 'Icecast2',      'abbr' => 'IC2',  'css' => 'sbadge-icecast2'],
    'shoutcast1'   => ['label' => 'Shoutcast v1',  'abbr' => 'SC1',  'css' => 'sbadge-sc1'],
    'shoutcast2'   => ['label' => 'Shoutcast v2',  'abbr' => 'SC2',  'css' => 'sbadge-sc2'],
    'steamcast'    => ['label' => 'Steamcast',     'abbr' => 'STM',  'css' => 'sbadge-steamcast'],
    'mcaster1dnas' => ['label' => 'Mcaster1DNAS',  'abbr' => 'DNAS', 'css' => 'sbadge-dnas'],
    /* We also map the encoder_configs server_protocol values for reuse in slot tables */
    'mcaster1'     => ['label' => 'Mcaster1DNAS',  'abbr' => 'DNAS', 'css' => 'sbadge-dnas'],
]);

/* ── Codec badge definitions ──────────────────────────────────────────────── */
define('MC1_CODEC_TYPES', [
    'mp3'      => ['label' => 'MP3',       'css' => 'cbadge-mp3'],
    'vorbis'   => ['label' => 'Vorbis',    'css' => 'cbadge-vorbis'],
    'opus'     => ['label' => 'Opus',      'css' => 'cbadge-opus'],
    'flac'     => ['label' => 'FLAC',      'css' => 'cbadge-flac'],
    'aac_lc'   => ['label' => 'AAC-LC',    'css' => 'cbadge-aac'],
    'aac_he'   => ['label' => 'AAC+',      'css' => 'cbadge-aache'],
    'aac_hev2' => ['label' => 'AAC++',     'css' => 'cbadge-aachev2'],
    'aac_eld'  => ['label' => 'AAC-ELD',   'css' => 'cbadge-aaceld'],
]);

/**
 * mc1_server_badge() — We return an HTML badge span for a server type.
 *
 * @param string $type   Server type key (icecast2, shoutcast1, shoutcast2, steamcast, mcaster1dnas)
 * @param bool   $abbr   If true we show the short abbreviation (IC2, SC1, etc.) instead of full label
 */
function mc1_server_badge(string $type, bool $abbr = false): string {
    $defs = MC1_SERVER_TYPES;
    $def  = $defs[$type] ?? ['label' => strtoupper($type), 'abbr' => strtoupper(substr($type, 0, 4)), 'css' => 'sbadge-unknown'];
    $text = $abbr ? h($def['abbr']) : h($def['label']);
    return '<span class="mc1-badge ' . h($def['css']) . '">' . $text . '</span>';
}

/**
 * mc1_codec_badge() — We return an HTML badge span for a codec format.
 *
 * @param string $codec   Codec key (mp3, opus, vorbis, flac, aac_lc, aac_he, aac_hev2, aac_eld)
 */
function mc1_codec_badge(string $codec): string {
    $defs = MC1_CODEC_TYPES;
    $def  = $defs[$codec] ?? ['label' => strtoupper($codec), 'css' => 'cbadge-unknown'];
    return '<span class="mc1-badge ' . h($def['css']) . '">' . h($def['label']) . '</span>';
}

/**
 * mc1_icons_css() — We return the complete CSS for all badge styles.
 * We output this inside a <style> block on any page that uses our badges.
 */
function mc1_icons_css(): string {
    return '
/* ── mc1 server type + codec badges ─────────────────────────────────────── */
.mc1-badge {
    display: inline-flex;
    align-items: center;
    padding: 2px 7px;
    border-radius: 4px;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: .04em;
    white-space: nowrap;
    vertical-align: middle;
    border: 1px solid transparent;
    line-height: 1.4;
}
/* Server type badges */
.sbadge-icecast2  { background:rgba(249,115,22,.15);  color:#f97316; border-color:rgba(249,115,22,.35); }
.sbadge-sc1       { background:rgba(20,184,166,.15);  color:#14b8a6; border-color:rgba(20,184,166,.35); }
.sbadge-sc2       { background:rgba(59,130,246,.15);  color:#60a5fa; border-color:rgba(59,130,246,.35); }
.sbadge-steamcast { background:rgba(139,92,246,.15);  color:#a78bfa; border-color:rgba(139,92,246,.35); }
.sbadge-dnas      { background:rgba(6,182,212,.15);   color:#22d3ee; border-color:rgba(6,182,212,.35); }
.sbadge-unknown   { background:rgba(100,116,139,.15); color:#94a3b8; border-color:rgba(100,116,139,.35); }
/* Codec badges */
.cbadge-mp3     { background:rgba(245,158,11,.15);  color:#f59e0b; border-color:rgba(245,158,11,.35); }
.cbadge-vorbis  { background:rgba(168,85,247,.15);  color:#c084fc; border-color:rgba(168,85,247,.35); }
.cbadge-opus    { background:rgba(59,130,246,.15);  color:#60a5fa; border-color:rgba(59,130,246,.35); }
.cbadge-flac    { background:rgba(34,197,94,.15);   color:#4ade80; border-color:rgba(34,197,94,.35); }
.cbadge-aac     { background:rgba(249,115,22,.15);  color:#f97316; border-color:rgba(249,115,22,.35); }
.cbadge-aache   { background:rgba(239,68,68,.15);   color:#f87171; border-color:rgba(239,68,68,.35); }
.cbadge-aachev2 { background:rgba(244,63,94,.15);   color:#fb7185; border-color:rgba(244,63,94,.35); }
.cbadge-aaceld  { background:rgba(236,72,153,.15);  color:#f472b6; border-color:rgba(236,72,153,.35); }
.cbadge-unknown { background:rgba(100,116,139,.15); color:#94a3b8; border-color:rgba(100,116,139,.35); }
';
}
