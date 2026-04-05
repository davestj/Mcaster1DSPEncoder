<?php
/**
 * forensic.php — Forensic Audio Analysis API
 *
 * File:    src/linux/web_ui/app/api/forensic.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   FA-1
 * Purpose: We provide API endpoints for forensic audio analysis: file metadata,
 *          annotation save/load, report export, and Ollama AI analysis integration.
 *
 * Actions (all POST JSON):
 *  analyze_file     — Returns file metadata: sample rate, bit depth, channels, duration, codec, SHA256
 *  save_annotations — Save annotation markers and analysis settings for a file
 *  load_annotations — Load saved annotations for a file
 *  export_report    — Generate HTML report data with annotations
 *  ai_analyze       — Send prompt to Ollama for audio spectrum analysis
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
}
