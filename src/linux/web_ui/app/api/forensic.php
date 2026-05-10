<?php
/**
 * forensic.php — Forensic Audio Analysis API
 *
 * File:    src/linux/web_ui/app/api/forensic.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   FA-3
 * Purpose: We provide API endpoints for forensic audio analysis: file metadata,
 *          annotation save/load, report export, Ollama AI analysis integration,
 *          server-side event detection, and report generation.
 *
 * Actions (all POST JSON):
 *  analyze_file        — Returns file metadata: sample rate, bit depth, channels, duration, codec, SHA256
 *  save_annotations    — Save annotation markers and analysis settings for a file
 *  load_annotations    — Load saved annotations for a file
 *  export_report       — Generate HTML report data with annotations
 *  ai_analyze          — Send prompt to Ollama for audio spectrum analysis
 *  ai_analyze_spectrum — Send spectrum data to Ollama with frequency distribution context
 *  generate_report     — Create self-contained HTML report server-side, return URL
 *  detect_events       — Server-side event detection using ffmpeg silence detect + peak detection
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active on this server
 *  - We use Mc1Db trait for all database access
 *  - We use first-person plural throughout all comments
 *  - We use raw SQL only, no ORMs or 3rd-party query builders
 *  - We escape all shell arguments with escapeshellarg() before any exec
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

if (!mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 401);
    return;
}

$user = mc1_current_user();
$userId = $user ? (int)$user['id'] : 0;

$input = json_decode(file_get_contents('php://input'), true);
$action = $input['action'] ?? '';

/* ── Ensure the forensic_analyses table exists ── */
ForensicApi::ensureTable();

if ($action === 'analyze_file') {
    ForensicApi::analyzeFile($input, $userId);
} elseif ($action === 'save_annotations') {
    ForensicApi::saveAnnotations($input, $userId);
} elseif ($action === 'load_annotations') {
    ForensicApi::loadAnnotations($input, $userId);
} elseif ($action === 'export_report') {
    ForensicApi::exportReport($input, $userId);
} elseif ($action === 'ai_analyze') {
    ForensicApi::aiAnalyze($input, $userId);
} elseif ($action === 'ai_analyze_spectrum') {
    ForensicApi::aiAnalyzeSpectrum($input, $userId);
} elseif ($action === 'generate_report') {
    ForensicApi::generateReportServer($input, $userId);
} elseif ($action === 'detect_events') {
    ForensicApi::detectEvents($input, $userId);
} else {
    mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
}

/* ═══════════════════════════════════════════════════════════════
 *  ForensicApi — All endpoint handlers
 * ═══════════════════════════════════════════════════════════════ */

class ForensicApi {
    use Mc1Db;

    /**
     * We ensure the forensic_analyses table exists in mcaster1_media.
     * We create it if missing (idempotent).
     */
    public static function ensureTable(): void
    {
        $sql = "CREATE TABLE IF NOT EXISTS forensic_analyses (
            id INT AUTO_INCREMENT PRIMARY KEY,
            user_id INT NOT NULL,
            file_path VARCHAR(512) NOT NULL,
            file_hash VARCHAR(64) DEFAULT NULL,
            analysis_name VARCHAR(255) DEFAULT NULL,
            annotations_json JSON DEFAULT NULL,
            settings_json JSON DEFAULT NULL COMMENT 'FFT size, window, colormap, etc.',
            notes TEXT DEFAULT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
            INDEX idx_user (user_id),
            INDEX idx_file (file_path(191))
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
        self::run('mcaster1_media', $sql);
    }

    /**
     * analyze_file — Use ffprobe to extract audio metadata and compute SHA256.
     */
    public static function analyzeFile(array $input, int $userId): void
    {
        $filePath = $input['file_path'] ?? '';
        if (empty($filePath) || !file_exists($filePath)) {
            mc1_api_respond(['error' => 'File not found'], 404);
            return;
        }

        /* Security: reject path traversal (SAST fix 2026-03-29) */
        if (strpos($filePath, '..') !== false) {
            mc1_api_respond(['error' => 'Invalid file path'], 400);
            return;
        }

        /* SHA256 hash */
        $hash = hash_file('sha256', $filePath);

        /* ffprobe metadata */
        $cmd = 'ffprobe -v quiet -print_format json -show_format -show_streams '
             . escapeshellarg($filePath) . ' 2>&1';
        $output = shell_exec($cmd);
        $probe = json_decode($output, true);

        $audioStream = null;
        if (!empty($probe['streams'])) {
            foreach ($probe['streams'] as $s) {
                if (($s['codec_type'] ?? '') === 'audio') {
                    $audioStream = $s;
                    break;
                }
            }
        }

        $format = $probe['format'] ?? [];

        mc1_api_respond([
            'ok' => true,
            'file_path' => $filePath,
            'file_hash' => $hash,
            'file_size' => filesize($filePath),
            'codec' => $audioStream['codec_name'] ?? 'unknown',
            'codec_long' => $audioStream['codec_long_name'] ?? '',
            'sample_rate' => (int)($audioStream['sample_rate'] ?? 0),
            'channels' => (int)($audioStream['channels'] ?? 0),
            'bit_depth' => $audioStream['bits_per_sample'] ?? $audioStream['bits_per_raw_sample'] ?? 0,
            'duration' => (float)($format['duration'] ?? 0),
            'bitrate' => (int)($format['bit_rate'] ?? 0),
            'format_name' => $format['format_name'] ?? '',
            'created' => date('Y-m-d H:i:s', filectime($filePath))
        ]);
    }

    /**
     * save_annotations — Persist annotation markers and settings to DB.
     */
    public static function saveAnnotations(array $input, int $userId): void
    {
        $filePath = $input['file_path'] ?? '';
        $fileHash = $input['file_hash'] ?? null;
        $name = $input['analysis_name'] ?? 'Untitled';
        $annotations = $input['annotations_json'] ?? [];
        $settings = $input['settings_json'] ?? [];
        $notes = $input['notes'] ?? '';

        if (empty($filePath)) {
            mc1_api_respond(['error' => 'file_path required'], 400);
            return;
        }

        $annoJson = json_encode($annotations);
        $settJson = json_encode($settings);

        $sql = "INSERT INTO forensic_analyses
                (user_id, file_path, file_hash, analysis_name, annotations_json, settings_json, notes)
                VALUES (?, ?, ?, ?, ?, ?, ?)";
        self::run('mcaster1_media', $sql, [
            $userId, $filePath, $fileHash, $name, $annoJson, $settJson, $notes
        ]);

        $id = self::lastId('mcaster1_media');
        mc1_api_respond(['ok' => true, 'id' => $id]);
    }

    /**
     * load_annotations — Retrieve saved analyses for a file (most recent first).
     */
    public static function loadAnnotations(array $input, int $userId): void
    {
        $filePath = $input['file_path'] ?? '';
        if (empty($filePath)) {
            mc1_api_respond(['error' => 'file_path required'], 400);
            return;
        }

        $sql = "SELECT id, analysis_name, annotations_json, settings_json, notes,
                       created_at, updated_at
                FROM forensic_analyses
                WHERE file_path = ? AND user_id = ?
                ORDER BY updated_at DESC
                LIMIT 20";
        $rows = self::rows('mcaster1_media', $sql, [$filePath, $userId]);

        mc1_api_respond(['ok' => true, 'analyses' => $rows]);
    }

    /**
     * export_report — Return structured data for client-side HTML report generation.
     */
    public static function exportReport(array $input, int $userId): void
    {
        $analysisId = (int)($input['id'] ?? 0);
        if ($analysisId < 1) {
            mc1_api_respond(['error' => 'id required'], 400);
            return;
        }

        $sql = "SELECT * FROM forensic_analyses WHERE id = ? AND user_id = ?";
        $row = self::row('mcaster1_media', $sql, [$analysisId, $userId]);

        if (!$row) {
            mc1_api_respond(['error' => 'Analysis not found'], 404);
            return;
        }

        mc1_api_respond([
            'ok' => true,
            'analysis' => $row
        ]);
    }

    /**
     * ai_analyze — Send a prompt to local Ollama instance for audio analysis.
     * We use the /api/generate endpoint on localhost:11434.
     */
    public static function aiAnalyze(array $input, int $userId): void
    {
        $prompt = $input['prompt'] ?? '';
        if (empty($prompt)) {
            mc1_api_respond(['error' => 'prompt required'], 400);
            return;
        }

        /* We attempt to reach the local Ollama instance */
        $ollamaUrl = 'http://127.0.0.1:11434/api/generate';
        $model = 'llama3.2';

        $payload = json_encode([
            'model' => $model,
            'prompt' => $prompt,
            'stream' => false,
            'options' => [
                'temperature' => 0.4,
                'num_predict' => 512
            ]
        ]);

        $ch = curl_init($ollamaUrl);
        curl_setopt_array($ch, [
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_POST => true,
            CURLOPT_POSTFIELDS => $payload,
            CURLOPT_HTTPHEADER => ['Content-Type: application/json'],
            CURLOPT_TIMEOUT => 60,
            CURLOPT_CONNECTTIMEOUT => 5
        ]);

        $result = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        $curlError = curl_error($ch);
        curl_close($ch);

        if ($curlError) {
            mc1_api_respond([
                'ok' => false,
                'error' => 'Ollama connection failed: ' . $curlError,
                'response' => 'AI analysis unavailable. Ollama may not be running on this server.'
            ]);
            return;
        }

        $data = json_decode($result, true);
        if ($httpCode === 200 && !empty($data['response'])) {
            mc1_api_respond([
                'ok' => true,
                'response' => $data['response'],
                'model' => $data['model'] ?? $model,
                'eval_count' => $data['eval_count'] ?? 0
            ]);
        } else {
            mc1_api_respond([
                'ok' => false,
                'error' => 'Ollama returned status ' . $httpCode,
                'response' => $data['error'] ?? 'Unknown AI error'
            ]);
        }
    }

    /**
     * ai_analyze_spectrum — Send spectrum data with frequency distribution context to Ollama.
     * We format the frequency bins into a human-readable prompt for the AI.
     */
    public static function aiAnalyzeSpectrum(array $input, int $userId): void
    {
        $spectrumData = $input['spectrum_data'] ?? '';
        $context = $input['context'] ?? '';
        $fileName = $input['file_name'] ?? 'unknown';

        if (empty($spectrumData) && empty($context)) {
            mc1_api_respond(['error' => 'spectrum_data or context required'], 400);
            return;
        }

        $prompt = "You are a forensic audio analyst. Analyze the following audio spectrum data.\n\n";
        $prompt .= "File: " . $fileName . "\n";
        if (!empty($context)) {
            $prompt .= $context . "\n\n";
        }
        if (!empty($spectrumData)) {
            $prompt .= "Frequency distribution:\n" . $spectrumData . "\n\n";
        }
        $prompt .= "Describe what you observe: identify dominant frequencies, harmonics, anomalies, "
                 . "possible sound sources, and any artifacts or unusual patterns.";

        /* We reuse the Ollama call logic */
        self::aiAnalyze(['prompt' => $prompt], $userId);
    }

    /**
     * generate_report — Create a self-contained HTML report server-side.
     * We store it in /tmp and return a URL for download.
     */
    public static function generateReportServer(array $input, int $userId): void
    {
        $analysisId = (int)($input['id'] ?? 0);
        $analystName = $input['analyst_name'] ?? 'Unknown';
        $caseNumber = $input['case_number'] ?? 'N/A';

        if ($analysisId < 1) {
            mc1_api_respond(['error' => 'id required'], 400);
            return;
        }

        $sql = "SELECT * FROM forensic_analyses WHERE id = ? AND user_id = ?";
        $row = self::row('mcaster1_media', $sql, [$analysisId, $userId]);

        if (!$row) {
            mc1_api_respond(['error' => 'Analysis not found'], 404);
            return;
        }

        $annotations = [];
        if (!empty($row['annotations_json'])) {
            $annotations = is_string($row['annotations_json'])
                ? json_decode($row['annotations_json'], true) : $row['annotations_json'];
        }

        $settings = [];
        if (!empty($row['settings_json'])) {
            $settings = is_string($row['settings_json'])
                ? json_decode($row['settings_json'], true) : $row['settings_json'];
        }

        /* We build a self-contained HTML report */
        $css = 'body{font-family:"Segoe UI",Helvetica,Arial,sans-serif;max-width:960px;margin:40px auto;color:#1e293b;line-height:1.6;padding:0 20px}'
             . 'h1{color:#0d9488;border-bottom:3px solid #0d9488;padding-bottom:12px}'
             . 'h2{color:#334155;margin-top:32px;border-bottom:1px solid #e2e8f0;padding-bottom:6px}'
             . 'table{border-collapse:collapse;width:100%;margin:16px 0;font-size:13px}'
             . 'th,td{border:1px solid #cbd5e1;padding:8px 12px;text-align:left}'
             . 'th{background:#f1f5f9;font-weight:600}'
             . '.chain-hash{font-family:monospace;font-size:12px;word-break:break-all;background:#f1f5f9;padding:8px;border-radius:4px}'
             . '.sig-block{margin-top:40px;padding:20px;border:2px solid #0d9488;border-radius:8px;background:#f0fdfa}';

        $html = '<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8">'
              . '<title>Forensic Report - ' . htmlspecialchars($row['file_path']) . '</title>'
              . '<style>' . $css . '</style></head><body>';

        $html .= '<h1>Forensic Audio Analysis Report</h1>';
        $html .= '<p><strong>Case Number:</strong> ' . htmlspecialchars($caseNumber)
               . ' | <strong>Analyst:</strong> ' . htmlspecialchars($analystName)
               . ' | <strong>Generated:</strong> ' . date('Y-m-d H:i:s') . '</p>';

        $html .= '<h2>File Information</h2><table>';
        $html .= '<tr><th>File Path</th><td>' . htmlspecialchars($row['file_path']) . '</td></tr>';
        $html .= '<tr><th>SHA-256</th><td><span class="chain-hash">' . htmlspecialchars($row['file_hash'] ?? 'N/A') . '</span></td></tr>';
        $html .= '<tr><th>Analysis Name</th><td>' . htmlspecialchars($row['analysis_name'] ?? '') . '</td></tr>';
        $html .= '<tr><th>Created</th><td>' . ($row['created_at'] ?? '') . '</td></tr>';
        $html .= '</table>';

        if (!empty($settings)) {
            $html .= '<h2>Analysis Settings</h2><table>';
            foreach ($settings as $key => $val) {
                $html .= '<tr><th>' . htmlspecialchars($key) . '</th><td>' . htmlspecialchars((string)$val) . '</td></tr>';
            }
            $html .= '</table>';
        }

        if (!empty($annotations)) {
            $html .= '<h2>Annotations (' . count($annotations) . ')</h2>';
            $html .= '<table><tr><th>#</th><th>Time</th><th>Frequency</th><th>Note</th></tr>';
            $idx = 1;
            foreach ($annotations as $a) {
                $time = isset($a['time']) ? gmdate('i:s', (int)$a['time']) . '.' . sprintf('%03d', fmod($a['time'], 1) * 1000) : '0:00';
                $freq = isset($a['freq']) ? round($a['freq']) . ' Hz' : '-';
                $note = $a['note'] ?? '';
                $html .= '<tr><td>' . $idx++ . '</td><td>' . $time . '</td><td>' . $freq
                       . '</td><td>' . htmlspecialchars($note) . '</td></tr>';
            }
            $html .= '</table>';
        }

        if (!empty($row['notes'])) {
            $html .= '<h2>Notes</h2><p>' . nl2br(htmlspecialchars($row['notes'])) . '</p>';
        }

        /* Signature block */
        $html .= '<div class="sig-block">';
        $html .= '<h3 style="color:#0d9488;margin:0 0 12px 0">Chain of Custody</h3>';
        $html .= '<p><strong>Analyst:</strong> ' . htmlspecialchars($analystName)
               . ' | <strong>Date:</strong> ' . date('Y-m-d')
               . ' | <strong>Case:</strong> ' . htmlspecialchars($caseNumber) . '</p>';
        $html .= '<p><strong>File Hash:</strong><br><span class="chain-hash">' . htmlspecialchars($row['file_hash'] ?? 'N/A') . '</span></p>';
        $html .= '<div style="margin-top:20px;border-top:1px solid #0d9488;padding-top:12px">'
               . '<p style="font-size:11px;color:#64748b">SIGNATURE:</p>'
               . '<div style="height:50px;border-bottom:1px solid #334155"></div></div>';
        $html .= '</div>';
        $html .= '</body></html>';

        /* Write to temp file */
        $tmpFile = '/tmp/forensic_report_' . $analysisId . '_' . time() . '.html';
        file_put_contents($tmpFile, $html);

        mc1_api_respond([
            'ok' => true,
            'report_path' => $tmpFile,
            'report_html' => $html
        ]);
    }

    /**
     * detect_events — Server-side event detection using ffmpeg silence detection
     * and simple peak analysis. We use ffmpeg's silencedetect filter and parse
     * the output for silence regions, then analyze for transients.
     */
    public static function detectEvents(array $input, int $userId): void
    {
        $filePath = $input['file_path'] ?? '';
        if (empty($filePath) || !file_exists($filePath)) {
            mc1_api_respond(['error' => 'File not found'], 404);
            return;
        }

        /* Security: reject path traversal (SAST fix 2026-03-29) */
        if (strpos($filePath, '..') !== false) {
            mc1_api_respond(['error' => 'Invalid file path'], 400);
            return;
        }

        $events = [];
        $noiseThresholdDB = (float)($input['noise_threshold_db'] ?? -50);
        $silenceDuration = (float)($input['silence_duration'] ?? 0.5);

        /* Silence detection using ffmpeg */
        $cmd = 'ffmpeg -i ' . escapeshellarg($filePath)
             . ' -af silencedetect=noise=' . escapeshellarg($noiseThresholdDB . 'dB')
             . ':d=' . escapeshellarg((string)$silenceDuration)
             . ' -f null - 2>&1';
        $output = shell_exec($cmd);

        if ($output) {
            $lines = explode("\n", $output);
            $silenceStart = null;
            foreach ($lines as $line) {
                /* Parse silence_start and silence_end lines */
                if (preg_match('/silence_start:\s*([\d.]+)/', $line, $m)) {
                    $silenceStart = (float)$m[1];
                }
                if (preg_match('/silence_end:\s*([\d.]+)\s*\|\s*silence_duration:\s*([\d.]+)/', $line, $m)) {
                    $events[] = [
                        'type' => 'silence',
                        'startTime' => $silenceStart ?? ((float)$m[1] - (float)$m[2]),
                        'endTime' => (float)$m[1],
                        'duration' => (float)$m[2],
                        'freq' => 0,
                        'magnitude' => $noiseThresholdDB,
                        'label' => 'Silence (' . round((float)$m[2] * 1000) . 'ms)'
                    ];
                    $silenceStart = null;
                }
            }
        }

        /* Peak detection using ffmpeg volumedetect */
        $cmd2 = 'ffmpeg -i ' . escapeshellarg($filePath)
              . ' -af volumedetect -f null - 2>&1';
        $output2 = shell_exec($cmd2);

        $maxVolume = null;
        $meanVolume = null;
        if ($output2) {
            if (preg_match('/max_volume:\s*([-\d.]+)\s*dB/', $output2, $m)) {
                $maxVolume = (float)$m[1];
            }
            if (preg_match('/mean_volume:\s*([-\d.]+)\s*dB/', $output2, $m)) {
                $meanVolume = (float)$m[1];
            }
        }

        /* Get file duration for context */
        $durationCmd = 'ffprobe -v quiet -show_entries format=duration -of csv=p=0 '
                     . escapeshellarg($filePath) . ' 2>&1';
        $duration = (float)trim(shell_exec($durationCmd) ?? '0');

        mc1_api_respond([
            'ok' => true,
            'events' => $events,
            'total_events' => count($events),
            'duration' => $duration,
            'max_volume' => $maxVolume,
            'mean_volume' => $meanVolume
        ]);
    }
}
