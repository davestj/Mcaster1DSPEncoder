<?php
/**
 * metrics.serverstats.class.php — DNAS / Icecast2 Server Stats Parser
 *
 * File:    src/linux/web_ui/app/inc/metrics.serverstats.class.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-02-23
 * Purpose: We fetch the raw XML stats from a Mcaster1DNAS or Icecast2 server,
 *          parse it into structured PHP arrays, compute derived metrics
 *          (uptime, bandwidth, per-mount health), and flag which mounts belong
 *          to our configured encoder slots versus external sources.
 *
 * Usage:
 *   $ss = new Mc1ServerStats([
 *       'host'       => 'dnas.mcaster1.com',
 *       'port'       => 9443,
 *       'ssl_enabled'=> true,
 *       'username'   => 'djpulse',
 *       'password'   => '...',
 *       'stats_path' => '/admin/mcaster1stats',  // Mcaster1DNAS
 *       // or '/status-json.xsl'                  // Icecast2
 *   ]);
 *   if ($ss->fetch()) {
 *       $data = $ss->get_data();  // ['global'=>[...], 'sources'=>[...]]
 *   }
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use first-person plural throughout all comments
 *  - We use mc1_db() only when a DB instance is passed in (not used directly)
 *
 * CHANGELOG:
 *  2026-02-23  v1.0.0  Initial implementation — XML + JSON parser, derived metrics,
 *                      per-mount flagging, bandwidth calculations
 */

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
    return;
}

class Mc1ServerStats {

    /* ── Config ───────────────────────────────────────────────────────────── */
    private string $host;
    private int    $port;
    private bool   $ssl;
    private string $username;
    private string $password;
    private string $stats_path;
    private int    $timeout;

    /* ── Results ──────────────────────────────────────────────────────────── */
    private ?array  $data    = null;
    private ?string $raw     = null;
    private ?string $error   = null;
    private int     $fetch_ms = 0;

    /**
     * __construct() — We accept a config array matching streaming_servers table fields.
     * We support both Mcaster1DNAS (XML at /admin/mcaster1stats) and
     * Icecast2 (JSON at /status-json.xsl or XML at /admin/stats).
     */
    public function __construct(array $config) {
        $this->host       = trim($config['host']       ?? '');
        $this->port       = (int)($config['port']      ?? 9443);
        $this->ssl        = (bool)($config['ssl_enabled'] ?? true);
        $this->username   = trim($config['username']   ?? $config['stat_username'] ?? '');
        $this->password   = trim($config['password']   ?? $config['stat_password'] ?? '');
        $this->stats_path = trim($config['stats_path'] ?? '/admin/mcaster1stats');
        $this->timeout    = (int)($config['timeout']   ?? 8);
    }

    /* ════════════════════════════════════════════════════════════════════════
     * fetch() — We make the cURL request to the stats endpoint and dispatch
     * to the correct parser based on content or path.
     * Returns true on success, false on failure.
     * ════════════════════════════════════════════════════════════════════════ */
    public function fetch(): bool {
        if (!function_exists('curl_init')) {
            $this->error = 'PHP cURL extension not available';
            return false;
        }
        if ($this->host === '') {
            $this->error = 'No host configured';
            return false;
        }

        $scheme = $this->ssl ? 'https' : 'http';
        $url    = "{$scheme}://{$this->host}:{$this->port}{$this->stats_path}";
        $t0     = microtime(true);

        $ch = curl_init();
        curl_setopt_array($ch, [
            CURLOPT_URL            => $url,
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_TIMEOUT        => $this->timeout,
            CURLOPT_CONNECTTIMEOUT => 5,
            CURLOPT_FOLLOWLOCATION => true,
            CURLOPT_MAXREDIRS      => 3,
            CURLOPT_SSL_VERIFYPEER => false,
            CURLOPT_SSL_VERIFYHOST => 0,
            CURLOPT_USERAGENT      => 'Mcaster1Encoder/1.4.0 (server-stats; +https://mcaster1.com)',
            CURLOPT_ENCODING       => 'gzip, deflate',
        ]);
        if ($this->username !== '') {
            curl_setopt($ch, CURLOPT_USERPWD, $this->username . ':' . $this->password);
            curl_setopt($ch, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        }

        $body      = curl_exec($ch);
        $http_code = (int)curl_getinfo($ch, CURLINFO_HTTP_CODE);
        $curl_err  = curl_error($ch);
        curl_close($ch);

        $this->fetch_ms = (int)round((microtime(true) - $t0) * 1000);

        if ($curl_err !== '' || $body === false) {
            $this->error = $curl_err ?: 'Connection failed';
            return false;
        }
        if ($http_code === 401 || $http_code === 403) {
            $this->error = "Authentication failed (HTTP {$http_code}) — check username/password";
            return false;
        }
        if ($http_code < 200 || $http_code >= 300) {
            $this->error = "HTTP {$http_code}";
            return false;
        }
        if (trim($body) === '') {
            $this->error = 'Empty response from server';
            return false;
        }

        $this->raw = $body;

        /* We dispatch to XML or JSON parser based on content sniff */
        $trimmed = ltrim($body);
        if ($trimmed[0] === '{' || $trimmed[0] === '[') {
            return $this->parse_icecast_json($body);
        }
        return $this->parse_mcaster1_xml($body);
    }

    /* ════════════════════════════════════════════════════════════════════════
     * parse_mcaster1_xml() — We parse Mcaster1DNAS <mcaster1stats> XML.
     * All per-source fields are extracted; derived metrics are computed.
     * ════════════════════════════════════════════════════════════════════════ */
    private function parse_mcaster1_xml(string $xml): bool {
        libxml_use_internal_errors(true);
        $dom = simplexml_load_string($xml);
        if ($dom === false) {
            $errs = libxml_get_errors();
            $this->error = 'XML parse failed: ' . (isset($errs[0]) ? trim($errs[0]->message) : 'unknown error');
            libxml_clear_errors();
            return false;
        }

        /* ── Global stats ─────────────────────────────────────────────────── */
        $global = [];
        foreach ($dom as $key => $val) {
            if ((string)$key === 'source') continue;
            $global[(string)$key] = (string)$val;
        }
        $global['_fetch_ms']   = $this->fetch_ms;
        $global['_uptime_s']   = $this->parse_server_start((string)($global['server_start'] ?? ''));
        $global['_uptime_str'] = self::format_duration($global['_uptime_s']);
        $global['_out_kbps']   = (int)($global['outgoing_kbitrate'] ?? 0);
        $global['_out_mbps']   = round($global['_out_kbps'] / 1024, 3);
        $global['_in_mb']      = round((int)($global['stream_kbytes_read'] ?? 0) / 1024, 1);
        $global['_out_mb']     = round((int)($global['stream_kbytes_sent'] ?? 0) / 1024, 1);
        $global['_listeners']  = (int)($global['listeners'] ?? 0);
        $global['_max_list']   = (int)($global['max_listeners'] ?? 0);
        $global['_sources']    = (int)($global['sources'] ?? 0);
        $global['_server_type'] = 'mcaster1dnas';

        /* ── Per-source stats ─────────────────────────────────────────────── */
        $sources = [];
        foreach ($dom->source as $src) {
            $mount = (string)$src['mount'];
            $s = ['_mount' => $mount];
            foreach ($src->children() as $k => $v) {
                $s[(string)$k] = (string)$v;
            }
            $s = $this->enrich_source($s);
            $sources[] = $s;
        }

        $this->data = ['global' => $global, 'sources' => $sources];
        return true;
    }

    /* ════════════════════════════════════════════════════════════════════════
     * parse_icecast_json() — We parse Icecast2 /status-json.xsl JSON.
     * We normalize fields to match our mcaster1 field names where possible.
     * ════════════════════════════════════════════════════════════════════════ */
    private function parse_icecast_json(string $json): bool {
        $d = json_decode($json, true);
        if (!is_array($d) || !isset($d['icestats'])) {
            $this->error = 'Unexpected JSON format (expected icestats root)';
            return false;
        }
        $ice = $d['icestats'];

        /* ── Global stats (we map Icecast field names to our convention) ── */
        $global = [
            'server_id'         => $ice['server_id']      ?? 'Icecast2',
            'admin'             => $ice['admin']           ?? '',
            'host'              => $ice['host']            ?? $this->host,
            'location'          => $ice['location']        ?? '',
            'server_start'      => $ice['server_start']    ?? '',
            'listeners'         => (string)($ice['listeners'] ?? 0),
            'max_listeners'     => (string)($ice['limits']['clients'] ?? 0),
            'sources'           => '',
            'outgoing_kbitrate' => '',
            '_fetch_ms'         => $this->fetch_ms,
            '_server_type'      => 'icecast2',
        ];
        $global['_uptime_s']   = $this->parse_server_start($global['server_start'] ?? '');
        $global['_uptime_str'] = self::format_duration($global['_uptime_s']);
        $global['_listeners']  = (int)($global['listeners']);
        $global['_max_list']   = (int)($global['max_listeners']);

        /* ── Per-source stats ─────────────────────────────────────────────── */
        $sources = [];
        $srcs = $ice['source'] ?? [];
        /* We handle both single source (object) and multiple sources (array) */
        if (isset($srcs['listenurl'])) { $srcs = [$srcs]; }
        $total_listeners = 0;
        $total_out_kbps  = 0;

        foreach ($srcs as $src) {
            $mount = parse_url($src['listenurl'] ?? '', PHP_URL_PATH) ?: '/?';
            $s = [
                '_mount'            => $mount,
                'server_name'       => $src['server_name'] ?? '',
                'server_description'=> $src['server_description'] ?? '',
                'server_type'       => $src['server_type'] ?? '',
                'server_url'        => $src['server_url'] ?? '',
                'genre'             => $src['genre'] ?? '',
                'listeners'         => (string)($src['listeners'] ?? 0),
                'listener_peak'     => (string)($src['listener_peak'] ?? 0),
                'bitrate'           => (string)($src['bitrate'] ?? 0),
                'outgoing_kbitrate' => (string)($src['stream_kbps'] ?? 0),
                'incoming_bitrate'  => '',
                'connected'         => '',
                'stream_start'      => $src['stream_start'] ?? '',
                'title'             => $src['title'] ?? '',
                'listenurl'         => $src['listenurl'] ?? '',
                'source_ip'         => $src['source_ip'] ?? '',
                'subtype'           => $src['subtype'] ?? '',
            ];
            $s = $this->enrich_source($s);
            $total_listeners += $s['_listeners'];
            $total_out_kbps  += $s['_out_kbps'];
            $sources[] = $s;
        }
        $global['sources']           = (string)count($sources);
        $global['_sources']          = count($sources);
        $global['outgoing_kbitrate'] = (string)$total_out_kbps;
        $global['_out_kbps']         = $total_out_kbps;
        $global['_out_mbps']         = round($total_out_kbps / 1024, 3);

        $this->data = ['global' => $global, 'sources' => $sources];
        return true;
    }

    /* ════════════════════════════════════════════════════════════════════════
     * enrich_source() — We compute derived fields for a single source array.
     * We call this for both XML and JSON parsed sources.
     * ════════════════════════════════════════════════════════════════════════ */
    private function enrich_source(array $s): array {
        $s['_in_kbps']     = (int)($s['incoming_bitrate'] ?? 0);
        $s['_out_kbps']    = (int)($s['outgoing_kbitrate'] ?? 0);
        $s['_listeners']   = (int)($s['listeners'] ?? 0);
        $s['_peak']        = (int)($s['listener_peak'] ?? 0);
        $s['_bitrate']     = (int)($s['bitrate'] ?? 0);
        $s['_connected_s'] = (int)($s['connected'] ?? 0);
        $s['_uptime']      = self::format_duration($s['_connected_s']);
        /* We consider a source ONLINE if it has any incoming bitrate or uptime */
        $s['_online']      = $s['_in_kbps'] > 0 || $s['_connected_s'] > 0;
        $s['_codec']       = $this->classify_codec((string)($s['server_type'] ?? ''), (string)($s['subtype'] ?? ''));
        /* We compute bandwidth in Mbps */
        $s['_in_mbps']     = round($s['_in_kbps'] / 1024, 3);
        $s['_out_mbps']    = round($s['_out_kbps'] / 1024, 3);
        /* We derive total sent in MB from bytes or already-computed mbytes */
        if (!empty($s['total_bytes_sent']) && (int)$s['total_bytes_sent'] > 0) {
            $s['_sent_mb'] = round((int)$s['total_bytes_sent'] / 1048576, 1);
        } else {
            $s['_sent_mb'] = (int)($s['total_mbytes_sent'] ?? 0);
        }
        /* We mark whether this is one of our encoder mounts (default false; set by mark_our_mounts) */
        $s['_ours']  = false;
        return $s;
    }

    /* ════════════════════════════════════════════════════════════════════════
     * mark_our_mounts() — We flag sources whose mount paths match the
     * server_mount values in encoder_configs. We also accept ad-hoc arrays
     * of mount path strings.
     * ════════════════════════════════════════════════════════════════════════ */
    public function mark_our_mounts(array $our_mounts): void {
        if (!$this->data) return;
        /* We normalize mount paths: ensure leading slash */
        $normalized = array_map(fn($m) => '/' . ltrim((string)$m, '/'), $our_mounts);
        foreach ($this->data['sources'] as &$src) {
            $src['_ours'] = in_array($src['_mount'], $normalized, true);
        }
        unset($src);
    }

    /* ════════════════════════════════════════════════════════════════════════
     * parse_server_start() — We parse the server_start string into a Unix
     * timestamp and return uptime in seconds.
     *
     * Mcaster1DNAS format: "23/Feb/2026:15:38:48 -0800"
     * Icecast2 format:     "Mon, 23 Feb 2026 15:38:48 -0800"
     * ════════════════════════════════════════════════════════════════════════ */
    private function parse_server_start(string $s): int {
        if ($s === '') return 0;
        /* We try Mcaster1DNAS format first: D/Mon/YYYY:HH:MM:SS tz */
        if (preg_match('#^(\d+)/(\w+)/(\d+):(\d+:\d+:\d+)\s*(.*)$#', $s, $m)) {
            $normalized = "{$m[1]} {$m[2]} {$m[3]} {$m[4]} {$m[5]}";
            $ts = strtotime($normalized);
        } else {
            /* We fall back to strtotime for RFC-style dates */
            $ts = strtotime($s);
        }
        if ($ts === false || $ts <= 0) return 0;
        return max(0, time() - $ts);
    }

    /* ════════════════════════════════════════════════════════════════════════
     * format_duration() — We convert seconds to a human-readable string.
     * We keep at most two units for readability.
     * ════════════════════════════════════════════════════════════════════════ */
    public static function format_duration(int $sec): string {
        if ($sec <= 0) return '—';
        $d = intdiv($sec, 86400); $sec %= 86400;
        $h = intdiv($sec, 3600);  $sec %= 3600;
        $m = intdiv($sec, 60);    $sec %= 60;
        $parts = [];
        if ($d) $parts[] = "{$d}d";
        if ($h) $parts[] = "{$h}h";
        if ($m) $parts[] = "{$m}m";
        if ($sec || empty($parts)) $parts[] = "{$sec}s";
        /* We limit to two parts to keep strings concise */
        return implode(' ', array_slice($parts, 0, 2));
    }

    /* ════════════════════════════════════════════════════════════════════════
     * classify_codec() — We map content-type MIME strings to short codec names.
     * We also check the subtype field for Ogg variants.
     * ════════════════════════════════════════════════════════════════════════ */
    private function classify_codec(string $type, string $subtype = '', string $icy2_codec = ''): string {
        $type = strtolower($type);
        /* We check ICY2 audio codec hint first — most reliable for our mounts */
        if ($icy2_codec !== '') {
            $ic = strtoupper(trim($icy2_codec));
            if ($ic === 'OPUS')   return 'Opus';
            if ($ic === 'VORBIS') return 'Vorbis';
            if ($ic === 'FLAC')   return 'FLAC';
            if ($ic === 'AAC+' || $ic === 'AACP') return 'AAC+';
            if ($ic === 'AAC')    return 'AAC';
            if ($ic === 'MP3')    return 'MP3';
        }
        if (str_contains($type, 'aacp') || str_contains($type, 'aac+')) return 'AAC+';
        if (str_contains($type, 'aac'))   return 'AAC';
        if (str_contains($type, 'mpeg'))  return 'MP3';
        if (str_contains($type, 'flac'))  return 'FLAC';
        if (str_contains($type, 'opus'))  return 'Opus';
        /* We handle both audio/ogg and application/ogg container types */
        if (str_contains($type, 'ogg') || $type === 'application/ogg') {
            /* We distinguish Vorbis, Opus, FLAC in Ogg container by subtype field */
            $sub = strtolower($subtype);
            if (str_contains($sub, 'opus'))   return 'Opus';
            if (str_contains($sub, 'vorbis')) return 'Vorbis';
            if (str_contains($sub, 'flac'))   return 'FLAC';
            return 'Ogg';
        }
        return $type !== '' ? strtoupper($type) : '—';
    }

    /* ── Accessors ────────────────────────────────────────────────────────── */
    public function get_data(): ?array  { return $this->data; }
    public function get_error(): ?string { return $this->error; }
    public function get_raw(): ?string  { return $this->raw; }
    public function get_fetch_ms(): int  { return $this->fetch_ms; }

    /**
     * get_summary() — We return a compact summary array for quick status display.
     * Used by the server table status cells (not the modal).
     */
    public function get_summary(): array {
        if (!$this->data) {
            return ['ok' => false, 'error' => $this->error, 'listeners' => 0];
        }
        $g = $this->data['global'];
        $online_sources = count(array_filter($this->data['sources'], fn($s) => $s['_online']));
        return [
            'ok'             => true,
            'listeners'      => $g['_listeners'],
            'max_listeners'  => $g['_max_list'],
            'sources_total'  => $g['_sources'],
            'sources_online' => $online_sources,
            'out_kbps'       => $g['_out_kbps'],
            'out_mbps'       => $g['_out_mbps'],
            'uptime'         => $g['_uptime_str'],
            'server_id'      => $g['server_id'] ?? '',
            'fetch_ms'       => $this->fetch_ms,
        ];
    }
}
