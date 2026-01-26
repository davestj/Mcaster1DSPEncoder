<?php
// metrics.datacalculate.class.php — DB query methods for metrics.php charts
//
// File:    src/linux/web_ui/app/inc/metrics.datacalculate.class.php
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-02-24
// Purpose: All SELECT queries needed to populate the 5 metrics dashboard tabs.
//          Uses Mc1Db trait; returns plain PHP arrays for JSON encoding.
//
// Note: No exit()/die() — uopz extension active.

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);
    return;
}

require_once __DIR__ . '/traits.db.class.php';
require_once __DIR__ . '/db.php';

class Mc1MetricsCalculator
{
    use Mc1Db;

    // ── System Health ────────────────────────────────────────────────────────

    // We return the last $minutes worth of system health snapshots for charts.
    public static function getSystemHealthHistory(int $minutes = 60): array
    {
        try {
            return self::rows(
                'mcaster1_metrics',
                "SELECT UNIX_TIMESTAMP(sampled_at) AS ts,
                        sampled_at,
                        cpu_pct, mem_used_mb, mem_total_mb,
                        ROUND(mem_used_mb/GREATEST(mem_total_mb,1)*100,1) AS mem_pct,
                        net_in_kbps, net_out_kbps, thread_count
                 FROM system_health_snapshots
                 WHERE sampled_at >= DATE_SUB(NOW(), INTERVAL ? MINUTE)
                 ORDER BY sampled_at ASC",
                [$minutes]
            );
        } catch (Exception $e) {
            return [];
        }
    }

    // We return encoder_health_snapshots for a slot for the last $hours hours.
    public static function getEncoderHealthHistory(int $slot_id, int $hours = 1): array
    {
        try {
            return self::rows(
                'mcaster1_metrics',
                "SELECT UNIX_TIMESTAMP(sampled_at) AS ts, sampled_at,
                        state, bytes_out, out_kbps, track_title, listeners
                 FROM encoder_health_snapshots
                 WHERE slot_id = ?
                   AND sampled_at >= DATE_SUB(NOW(), INTERVAL ? HOUR)
                 ORDER BY sampled_at ASC",
                [$slot_id, $hours]
            );
        } catch (Exception $e) {
            return [];
        }
    }

    // ── Streaming Servers ────────────────────────────────────────────────────

    // We return the list of configured streaming servers with last status.
    public static function getServerList(): array
    {
        try {
            return self::rows(
                'mcaster1_encoder',
                "SELECT id, name, server_type, host, port, ssl_enabled,
                        is_active, display_order,
                        last_status, last_listeners, last_checked,
                        poll_interval_s
                 FROM streaming_servers
                 WHERE is_active = 1
                 ORDER BY display_order, id"
            );
        } catch (Exception $e) {
            return [];
        }
    }

    // We return server_stat_history for one server for the last $hours hours.
    public static function getServerStatHistory(int $server_id, int $hours = 6): array
    {
        try {
            return self::rows(
                'mcaster1_metrics',
                "SELECT UNIX_TIMESTAMP(sampled_at) AS ts, sampled_at,
                        listeners, out_kbps, sources_online, status
                 FROM server_stat_history
                 WHERE server_id = ?
                   AND sampled_at >= DATE_SUB(NOW(), INTERVAL ? HOUR)
                 ORDER BY sampled_at ASC",
                [$server_id, $hours]
            );
        } catch (Exception $e) {
            return [];
        }
    }

    // ── Listener Analytics ───────────────────────────────────────────────────

    // We return summary listener stats for the last $days days.
    public static function getListenerSummary(int $days = 30): array
    {
        try {
            $row = self::row(
                'mcaster1_metrics',
                "SELECT
                    COUNT(*)                               AS total_sessions,
                    COUNT(DISTINCT client_ip)              AS unique_ips,
                    COALESCE(SUM(duration_sec)/3600, 0)   AS total_hours,
                    COALESCE(AVG(duration_sec)/60,   0)   AS avg_listen_min,
                    COALESCE(SUM(bytes_sent),        0)   AS total_bytes
                 FROM listener_sessions
                 WHERE connected_at >= DATE_SUB(NOW(), INTERVAL ? DAY)
                   AND disconnected_at IS NOT NULL",
                [$days]
            );
            return $row ?? ['total_sessions'=>0,'unique_ips'=>0,'total_hours'=>0,'avg_listen_min'=>0,'total_bytes'=>0];
        } catch (Exception $e) {
            return ['total_sessions'=>0,'unique_ips'=>0,'total_hours'=>0,'avg_listen_min'=>0,'total_bytes'=>0];
        }
    }

    // We return daily session + peak data for Chart.js line chart.
    public static function getDailyListenerTrend(int $days = 30): array
    {
        try {
            return self::rows(
                'mcaster1_metrics',
                "SELECT stat_date,
                        SUM(total_sessions)  AS sessions,
                        MAX(peak_listeners)  AS peak,
                        SUM(total_hours)     AS hours
                 FROM daily_stats
                 WHERE stat_date >= DATE_SUB(CURDATE(), INTERVAL ? DAY)
                 GROUP BY stat_date
                 ORDER BY stat_date ASC",
                [$days]
            );
        } catch (Exception $e) {
            return [];
        }
    }

    // We return per-mount session breakdown for the doughnut chart.
    public static function getMountBreakdown(int $days = 30): array
    {
        try {
            return self::rows(
                'mcaster1_metrics',
                "SELECT stream_mount,
                        COUNT(*) AS sessions,
                        ROUND(SUM(duration_sec)/3600, 1) AS hours
                 FROM listener_sessions
                 WHERE connected_at >= DATE_SUB(NOW(), INTERVAL ? DAY)
                 GROUP BY stream_mount
                 ORDER BY sessions DESC",
                [$days]
            );
        } catch (Exception $e) {
            return [];
        }
    }

    // We return top tracks by play_count from track_plays or tracks fallback.
    public static function getTopTracks(int $limit = 20): array
    {
        $limit = min(100, max(1, $limit));
        try {
            // Prefer track_plays aggregate (populated by C++ encoder_slot)
            $rows = self::rows(
                'mcaster1_metrics',
                "SELECT title, artist, COUNT(*) AS play_count,
                        MAX(played_at) AS last_played_at
                 FROM track_plays
                 GROUP BY title, artist
                 ORDER BY play_count DESC
                 LIMIT ?",
                [$limit]
            );
            if (!empty($rows)) return $rows;
        } catch (Exception $e) {}

        // Fallback: tracks.play_count
        try {
            return self::rows(
                'mcaster1_media',
                "SELECT title, artist, play_count, last_played_at
                 FROM tracks
                 WHERE play_count > 0
                 ORDER BY play_count DESC
                 LIMIT ?",
                [$limit]
            );
        } catch (Exception $e) {
            return [];
        }
    }

    // ── Event Log ────────────────────────────────────────────────────────────

    // We return paginated stream_events with optional filters.
    public static function getStreamEvents(int $page = 1, int $limit = 50, ?int $slot_id = null, ?string $event_type = null): array
    {
        $limit  = min(200, max(1, $limit));
        $offset = ($page - 1) * $limit;

        $where  = [];
        $params = [];
        if ($slot_id !== null) {
            $where[]  = 'slot_id = ?';
            $params[] = $slot_id;
        }
        if ($event_type !== null && $event_type !== '') {
            $where[]  = 'event_type = ?';
            $params[] = $event_type;
        }
        $where_sql = $where ? 'WHERE ' . implode(' AND ', $where) : '';

        try {
            $total = (int)self::scalar(
                'mcaster1_metrics',
                "SELECT COUNT(*) FROM stream_events $where_sql",
                $params
            );
            $rows = self::rows(
                'mcaster1_metrics',
                "SELECT id, slot_id, mount, event_type, detail, created_at
                 FROM stream_events $where_sql
                 ORDER BY created_at DESC
                 LIMIT ? OFFSET ?",
                array_merge($params, [$limit, $offset])
            );
            return [
                'total' => $total,
                'pages' => max(1, (int)ceil($total / $limit)),
                'page'  => $page,
                'data'  => $rows,
            ];
        } catch (Exception $e) {
            return ['total' => 0, 'pages' => 1, 'page' => 1, 'data' => []];
        }
    }

    // We return recent listener sessions (for the analytics tab table).
    public static function getRecentSessions(int $limit = 50, string $mount = ''): array
    {
        $limit = min(200, max(1, $limit));
        $where  = $mount !== '' ? 'WHERE stream_mount = ?' : '';
        $params = $mount !== '' ? [$mount, $limit] : [$limit];
        try {
            return self::rows(
                'mcaster1_metrics',
                "SELECT client_ip, user_agent, stream_mount,
                        connected_at, disconnected_at, duration_sec, bytes_sent
                 FROM listener_sessions $where
                 ORDER BY connected_at DESC
                 LIMIT ?",
                $params
            );
        } catch (Exception $e) {
            return [];
        }
    }
}
