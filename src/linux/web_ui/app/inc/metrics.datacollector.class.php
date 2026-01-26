<?php
// metrics.datacollector.class.php — Data collection utilities for mcaster1_metrics
//
// File:    src/linux/web_ui/app/inc/metrics.datacollector.class.php
// Author:  Dave St. John <davestj@gmail.com>
// Date:    2026-02-24
// Purpose: Maintenance operations: pruning old rows, aggregating daily stats.
//          These methods are for CLI / cron context only — not called from page
//          PHP during a FastCGI request.  The C++ background threads handle
//          real-time inserts; this class handles housekeeping.
//
// Note: No exit()/die() — uopz extension active. Use return inside functions.

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo json_encode(['ok' => false, 'error' => 'Forbidden']);
    return;
}

require_once __DIR__ . '/traits.db.class.php';
require_once __DIR__ . '/db.php';

class Mc1MetricsCollector
{
    use Mc1Db;

    // ── pruneOldData ─────────────────────────────────────────────────────────
    // We delete rows older than $days days from all metrics history tables.

    public static function pruneOldData(int $days = 7): array
    {
        $days = max(1, $days);
        $deleted = [];
        $tables = [
            'mcaster1_metrics' => [
                'system_health_snapshots'  => 'sampled_at',
                'encoder_health_snapshots' => 'sampled_at',
                'server_stat_history'      => 'sampled_at',
            ],
        ];

        foreach ($tables as $db => $tbl_map) {
            foreach ($tbl_map as $table => $col) {
                try {
                    $n = self::run(
                        $db,
                        "DELETE FROM {$table} WHERE {$col} < DATE_SUB(NOW(), INTERVAL ? DAY)",
                        [$days]
                    );
                    $deleted[$table] = $n;
                } catch (Exception $e) {
                    $deleted[$table] = 'error: ' . $e->getMessage();
                }
            }
        }

        // Also prune disconnected listener_sessions older than 90 days
        try {
            $n = self::run(
                'mcaster1_metrics',
                "DELETE FROM listener_sessions
                 WHERE disconnected_at IS NOT NULL
                   AND disconnected_at < DATE_SUB(NOW(), INTERVAL ? DAY)",
                [90]
            );
            $deleted['listener_sessions_old'] = $n;
        } catch (Exception $e) {
            $deleted['listener_sessions_old'] = 'error: ' . $e->getMessage();
        }

        return $deleted;
    }

    // ── aggregateDailyStats ──────────────────────────────────────────────────
    // We aggregate completed listener_sessions into daily_stats for $date.
    // If $date is null, we use yesterday (so we aggregate completed days only).

    public static function aggregateDailyStats(?string $date = null): bool
    {
        if ($date === null) {
            $date = date('Y-m-d', strtotime('-1 day'));
        }

        try {
            // We upsert one row per mount per date.
            $n = self::run(
                'mcaster1_metrics',
                "INSERT INTO daily_stats
                    (stat_date, mount, peak_listeners, total_sessions, total_hours, tracks_played)
                 SELECT
                    DATE(connected_at)   AS stat_date,
                    stream_mount         AS mount,
                    0                    AS peak_listeners,
                    COUNT(*)             AS total_sessions,
                    COALESCE(SUM(duration_sec), 0) / 3600.0 AS total_hours,
                    0                    AS tracks_played
                 FROM listener_sessions
                 WHERE DATE(connected_at) = ?
                   AND disconnected_at IS NOT NULL
                 GROUP BY DATE(connected_at), stream_mount
                 ON DUPLICATE KEY UPDATE
                    total_sessions = VALUES(total_sessions),
                    total_hours    = VALUES(total_hours)",
                [$date]
            );
            return $n >= 0;
        } catch (Exception $e) {
            return false;
        }
    }
}
