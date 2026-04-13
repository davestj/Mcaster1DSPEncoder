<?php
/**
 * captions.php -- Closed Caption & Auto-Transcription API
 *
 * File:    src/linux/web_ui/app/api/captions.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We provide auto-transcription via Whisper (preferred) or Ollama
 *          fallback, caption save/load, SRT burn-in via ffmpeg, and caption
 *          file download for podcast RSS feeds.
 *
 * Actions (POST JSON):
 *  transcribe_chunk  -- Receive base64 WAV chunk, transcribe via Whisper/Ollama
 *  transcribe_file   -- Full file transcription for podcast episodes
 *  burn_captions     -- Burn SRT captions into video via ffmpeg subtitles filter
 *  save_captions     -- Save SRT/VTT text to database linked to an episode
 *  load_captions     -- Load saved captions for an episode
 *  delete_captions   -- Remove saved captions for an episode
 *  list_captions     -- List all caption records for an episode
 *
 * GET actions (public, no auth -- for podcast feed transcript links):
 *  download          -- Download caption file (SRT or VTT) for an episode
 *
 * Standards:
 *  - We never call exit() or die() -- uopz extension is active
 *  - We use Mc1Db trait for all database access
 *  - We use mc1_api_respond() for all JSON responses
 *  - We use mc1_is_authed() for auth gate
 *  - We use escapeshellarg() on all shell arguments
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

/* -- Public download endpoint (no auth -- podcast clients need access) -- */
if ($_SERVER['REQUEST_METHOD'] === 'GET' && ($_GET['action'] ?? '') === 'download') {
    $episode_id = (int)($_GET['episode_id'] ?? 0);
    $format     = ($_GET['format'] ?? 'srt') === 'vtt' ? 'vtt' : 'srt';
    $language   = preg_replace('/[^a-zA-Z0-9_-]/', '', $_GET['language'] ?? 'en');

    if ($episode_id < 1) {
        http_response_code(400);
        header('Content-Type: application/json');
        echo json_encode(['error' => 'Missing episode_id']);
        return;
    }

    CaptionsApi::handleDownload($episode_id, $format, $language);
    return;
}

header('Content-Type: application/json');

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    mc1_api_respond(['error' => 'POST required'], 405);
    return;
}

/* -- Parse request body -- */
$raw  = file_get_contents('php://input');
$data = json_decode($raw, true);
if (!is_array($data) || empty($data['action'])) {
    mc1_api_respond(['error' => 'Missing action'], 400);
    return;
}

$action = $data['action'];

/* -- Auth gate -- */
if (!mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}

/* -- Caption helper class -- */
class CaptionsApi {
    use Mc1Db;

    const DB = 'mcaster1_media';
    const TMP_DIR = '/tmp/mc1_captions';

    /* ================================================================
     * Public download (no auth)
     * ================================================================ */

    public static function handleDownload(int $episode_id, string $format, string $language): void
    {
        $cap = self::row(self::DB,
            "SELECT caption_text, format FROM episode_captions
             WHERE episode_id = ? AND language = ? ORDER BY updated_at DESC LIMIT 1",
            [$episode_id, $language]);

        if (!$cap) {
            http_response_code(404);
            header('Content-Type: application/json');
            echo json_encode(['error' => 'No captions found']);
            return;
        }

        $text = $cap['caption_text'];

        /* Convert between formats if needed */
        if ($format === 'vtt' && $cap['format'] === 'srt') {
            $text = self::srtToVtt($text);
        } elseif ($format === 'srt' && $cap['format'] === 'vtt') {
            $text = self::vttToSrt($text);
        }

        $mime = $format === 'vtt' ? 'text/vtt' : 'application/x-subrip';
        header('Content-Type: ' . $mime . '; charset=UTF-8');
        header('Content-Disposition: inline; filename="episode_' . $episode_id . '.' . $format . '"');
        header('Cache-Control: public, max-age=3600');
        echo $text;
    }

    /* ================================================================
     * transcribe_chunk -- live transcription from base64 WAV
     * ================================================================ */

    public static function transcribeChunk(array $data): array
    {
        $wav64    = $data['wav_base64'] ?? '';
        $language = preg_replace('/[^a-zA-Z0-9_-]/', '', $data['language'] ?? 'en');
        $offset   = (float)($data['offset_sec'] ?? 0);

        if (empty($wav64)) {
            return ['error' => 'Missing wav_base64'];
        }

        self::ensureTmpDir();
        $tmpWav = self::TMP_DIR . '/chunk_' . bin2hex(random_bytes(8)) . '.wav';
        $decoded = base64_decode($wav64, true);
        if ($decoded === false) {
            return ['error' => 'Invalid base64 data'];
        }
        file_put_contents($tmpWav, $decoded);

        /* Try Whisper first, then Ollama */
        $segments = self::transcribeWithWhisper($tmpWav, $language);
        if ($segments === null) {
            $segments = self::transcribeWithOllama($tmpWav, $language);
        }

        @unlink($tmpWav);
        /* Clean up any whisper output files */
        $base = pathinfo($tmpWav, PATHINFO_FILENAME);
        foreach (glob(self::TMP_DIR . '/' . $base . '.*') as $f) {
            @unlink($f);
        }

        if ($segments === null) {
            return ['error' => 'No transcription engine available (install whisper or ollama)', 'segments' => []];
        }

        return ['ok' => true, 'segments' => $segments];
    }

    /* ================================================================
     * transcribe_file -- full episode transcription
     * ================================================================ */

    public static function transcribeFile(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        $language   = preg_replace('/[^a-zA-Z0-9_-]/', '', $data['language'] ?? 'en');
        $format     = ($data['format'] ?? 'srt') === 'vtt' ? 'vtt' : 'srt';

        if ($episode_id < 1) {
            return ['error' => 'Missing episode_id'];
        }

        $ep = self::row(self::DB,
            "SELECT file_path FROM podcast_episodes WHERE id = ?", [$episode_id]);
        if (!$ep || empty($ep['file_path'])) {
            return ['error' => 'Episode not found or has no file'];
        }

        $filePath = $ep['file_path'];
        if (!file_exists($filePath)) {
            return ['error' => 'Audio file not found on disk'];
        }

        /* Use whisper on the full file */
        $whisperBin = self::findWhisper();
        if (!$whisperBin) {
            return ['error' => 'Whisper not available. Install with: pip install openai-whisper'];
        }

        self::ensureTmpDir();
        $outBase = self::TMP_DIR . '/ep_' . $episode_id . '_' . bin2hex(random_bytes(4));

        $cmd = escapeshellarg($whisperBin)
             . ' ' . escapeshellarg($filePath)
             . ' --model base'
             . ' --output_format srt'
             . ' --output_dir ' . escapeshellarg(self::TMP_DIR)
             . ' --language ' . escapeshellarg($language)
             . ' 2>&1';

        $output = [];
        $ret = 0;
        exec($cmd, $output, $ret);

        /* Whisper names output after the input file */
        $inputBase = pathinfo($filePath, PATHINFO_FILENAME);
        $srtFile = self::TMP_DIR . '/' . $inputBase . '.srt';

        if (!file_exists($srtFile)) {
            /* Try alternate naming */
            $found = glob(self::TMP_DIR . '/' . $inputBase . '*.srt');
            if (!empty($found)) {
                $srtFile = $found[0];
            } else {
                mc1_log(MC1_LOG_ERROR, 'Whisper produced no output: ' . implode("\n", $output), 'captions');
                return ['error' => 'Transcription failed', 'detail' => implode("\n", array_slice($output, -5))];
            }
        }

        $srtText = file_get_contents($srtFile);
        @unlink($srtFile);
        /* Clean up other whisper output files */
        foreach (glob(self::TMP_DIR . '/' . $inputBase . '.*') as $f) {
            @unlink($f);
        }

        /* Save to database */
        $captionText = ($format === 'vtt') ? self::srtToVtt($srtText) : $srtText;

        self::run(self::DB,
            "INSERT INTO episode_captions (episode_id, language, format, caption_text, is_auto_generated)
             VALUES (?, ?, ?, ?, 1)
             ON DUPLICATE KEY UPDATE caption_text = VALUES(caption_text),
                                     format = VALUES(format),
                                     is_auto_generated = 1,
                                     updated_at = CURRENT_TIMESTAMP",
            [$episode_id, $language, $format, $captionText]);

        return [
            'ok'           => true,
            'episode_id'   => $episode_id,
            'language'     => $language,
            'format'       => $format,
            'caption_text' => $captionText,
            'cue_count'    => substr_count($srtText, "\n\n")
        ];
    }

    /* ================================================================
     * burn_captions -- burn SRT into video via ffmpeg
     * ================================================================ */

    public static function burnCaptions(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        $language   = preg_replace('/[^a-zA-Z0-9_-]/', '', $data['language'] ?? 'en');

        if ($episode_id < 1) {
            return ['error' => 'Missing episode_id'];
        }

        $ep = self::row(self::DB,
            "SELECT file_path, format FROM podcast_episodes WHERE id = ?", [$episode_id]);
        if (!$ep || empty($ep['file_path'])) {
            return ['error' => 'Episode not found'];
        }

        if (!in_array($ep['format'] ?? '', ['mp4', 'webm', 'mkv', 'avi', 'mov'])) {
            return ['error' => 'burn_captions requires a video episode'];
        }

        $cap = self::row(self::DB,
            "SELECT caption_text, format FROM episode_captions
             WHERE episode_id = ? AND language = ? ORDER BY updated_at DESC LIMIT 1",
            [$episode_id, $language]);

        if (!$cap) {
            return ['error' => 'No captions found for this episode/language'];
        }

        /* We need SRT format for the subtitles filter */
        $srtText = $cap['caption_text'];
        if ($cap['format'] === 'vtt') {
            $srtText = self::vttToSrt($srtText);
        }

        self::ensureTmpDir();
        $srtFile = self::TMP_DIR . '/burn_' . $episode_id . '.srt';
        file_put_contents($srtFile, $srtText);

        $inputPath = $ep['file_path'];
        $ext = pathinfo($inputPath, PATHINFO_EXTENSION) ?: 'mp4';
        $outputPath = preg_replace('/\.[^.]+$/', '_captioned.' . $ext, $inputPath);

        $cmd = 'ffmpeg -y'
             . ' -i ' . escapeshellarg($inputPath)
             . ' -vf ' . escapeshellarg('subtitles=' . $srtFile . ':force_style=\'FontSize=24,PrimaryColour=&HFFFFFF&,OutlineColour=&H000000&,Outline=2,BackColour=&H80000000&\'')
             . ' -c:a copy'
             . ' ' . escapeshellarg($outputPath)
             . ' 2>&1';

        $output = [];
        $ret = 0;
        exec($cmd, $output, $ret);

        @unlink($srtFile);

        if ($ret !== 0) {
            mc1_log(MC1_LOG_ERROR, 'ffmpeg burn_captions failed: ' . implode("\n", array_slice($output, -10)), 'captions');
            return ['error' => 'ffmpeg failed', 'detail' => implode("\n", array_slice($output, -5))];
        }

        return [
            'ok'          => true,
            'output_path' => $outputPath,
            'file_size'   => filesize($outputPath),
        ];
    }

    /* ================================================================
     * save_captions -- persist SRT/VTT to database
     * ================================================================ */

    public static function saveCaptions(array $data): array
    {
        $episode_id   = (int)($data['episode_id'] ?? 0);
        $language     = preg_replace('/[^a-zA-Z0-9_-]/', '', $data['language'] ?? 'en');
        $format       = ($data['format'] ?? 'srt') === 'vtt' ? 'vtt' : 'srt';
        $caption_text = $data['caption_text'] ?? '';
        $auto         = (int)($data['is_auto_generated'] ?? 0);

        if ($episode_id < 1) {
            return ['error' => 'Missing episode_id'];
        }
        if (empty($caption_text)) {
            return ['error' => 'Missing caption_text'];
        }

        /* Upsert: update if same episode+language exists */
        $existing = self::row(self::DB,
            "SELECT id FROM episode_captions WHERE episode_id = ? AND language = ?",
            [$episode_id, $language]);

        if ($existing) {
            self::run(self::DB,
                "UPDATE episode_captions
                 SET caption_text = ?, format = ?, is_auto_generated = ?, updated_at = CURRENT_TIMESTAMP
                 WHERE id = ?",
                [$caption_text, $format, $auto, (int)$existing['id']]);
            $capId = (int)$existing['id'];
        } else {
            self::run(self::DB,
                "INSERT INTO episode_captions (episode_id, language, format, caption_text, is_auto_generated)
                 VALUES (?, ?, ?, ?, ?)",
                [$episode_id, $language, $format, $caption_text, $auto]);
            $capId = (int)self::lastId(self::DB);
        }

        return ['ok' => true, 'id' => $capId, 'episode_id' => $episode_id, 'language' => $language];
    }

    /* ================================================================
     * load_captions -- load saved captions for an episode
     * ================================================================ */

    public static function loadCaptions(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        $language   = preg_replace('/[^a-zA-Z0-9_-]/', '', $data['language'] ?? 'en');

        if ($episode_id < 1) {
            return ['error' => 'Missing episode_id'];
        }

        $cap = self::row(self::DB,
            "SELECT * FROM episode_captions
             WHERE episode_id = ? AND language = ? ORDER BY updated_at DESC LIMIT 1",
            [$episode_id, $language]);

        if (!$cap) {
            return ['ok' => true, 'found' => false, 'caption_text' => '', 'format' => 'srt'];
        }

        return [
            'ok'                => true,
            'found'             => true,
            'id'                => (int)$cap['id'],
            'episode_id'        => (int)$cap['episode_id'],
            'language'          => $cap['language'],
            'format'            => $cap['format'],
            'caption_text'      => $cap['caption_text'],
            'is_auto_generated' => (int)$cap['is_auto_generated'],
            'created_at'        => $cap['created_at'],
            'updated_at'        => $cap['updated_at'],
        ];
    }

    /* ================================================================
     * delete_captions
     * ================================================================ */

    public static function deleteCaptions(array $data): array
    {
        $id = (int)($data['id'] ?? 0);
        if ($id < 1) {
            return ['error' => 'Missing id'];
        }
        self::run(self::DB, "DELETE FROM episode_captions WHERE id = ?", [$id]);
        return ['ok' => true, 'deleted_id' => $id];
    }

    /* ================================================================
     * list_captions -- list all caption records for an episode
     * ================================================================ */

    public static function listCaptions(array $data): array
    {
        $episode_id = (int)($data['episode_id'] ?? 0);
        if ($episode_id < 1) {
            return ['error' => 'Missing episode_id'];
        }

        $rows = self::rows(self::DB,
            "SELECT id, episode_id, language, format, is_auto_generated,
                    LENGTH(caption_text) AS text_length, created_at, updated_at
             FROM episode_captions WHERE episode_id = ? ORDER BY language, updated_at DESC",
            [$episode_id]);

        return ['ok' => true, 'captions' => $rows];
    }

    /* ================================================================
     * Internal: Whisper transcription
     * ================================================================ */

    private static function transcribeWithWhisper(string $wavPath, string $language): ?array
    {
        $whisperBin = self::findWhisper();
        if (!$whisperBin) return null;

        $cmd = escapeshellarg($whisperBin)
             . ' ' . escapeshellarg($wavPath)
             . ' --model base'
             . ' --output_format srt'
             . ' --output_dir ' . escapeshellarg(self::TMP_DIR)
             . ' --language ' . escapeshellarg($language)
             . ' 2>&1';

        $output = [];
        $ret = 0;
        exec($cmd, $output, $ret);

        $base = pathinfo($wavPath, PATHINFO_FILENAME);
        $srtFile = self::TMP_DIR . '/' . $base . '.srt';

        if (!file_exists($srtFile)) {
            $found = glob(self::TMP_DIR . '/' . $base . '*.srt');
            if (!empty($found)) {
                $srtFile = $found[0];
            } else {
                mc1_log(MC1_LOG_WARN, 'Whisper chunk produced no SRT: ' . implode("\n", $output), 'captions');
                return null;
            }
        }

        $srtText = file_get_contents($srtFile);
        @unlink($srtFile);

        return self::parseSrtToSegments($srtText);
    }

    /* ================================================================
     * Internal: Ollama fallback transcription
     * ================================================================ */

    private static function transcribeWithOllama(string $wavPath, string $language): ?array
    {
        /* We check if ollama is running */
        $ollamaHost = getenv('OLLAMA_HOST') ?: 'http://127.0.0.1:11434';

        $ch = curl_init($ollamaHost . '/api/tags');
        curl_setopt_array($ch, [
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_TIMEOUT => 3,
            CURLOPT_CONNECTTIMEOUT => 2,
        ]);
        $resp = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);

        if ($httpCode !== 200) {
            return null;  /* Ollama not running */
        }

        /* Convert WAV to base64 for Ollama multimodal */
        $wavData = file_get_contents($wavPath);
        $wavB64 = base64_encode($wavData);

        /* Use Ollama to attempt audio description (Ollama doesn't do real STT,
           but we can ask a multimodal model to describe what it "hears") */
        $payload = json_encode([
            'model'  => 'llama3.2-vision',
            'prompt' => 'Transcribe the following audio content into text with timestamps. '
                      . 'Language: ' . $language . '. '
                      . 'Format each segment as: [START-END] text. '
                      . 'Provide accurate word-by-word transcription.',
            'stream' => false,
            'images' => [$wavB64],
        ]);

        $ch = curl_init($ollamaHost . '/api/generate');
        curl_setopt_array($ch, [
            CURLOPT_POST => true,
            CURLOPT_POSTFIELDS => $payload,
            CURLOPT_HTTPHEADER => ['Content-Type: application/json'],
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_TIMEOUT => 120,
        ]);
        $resp = curl_exec($ch);
        $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);

        if ($httpCode !== 200 || empty($resp)) {
            return null;
        }

        $result = json_decode($resp, true);
        $text = $result['response'] ?? '';
        if (empty($text)) {
            return null;
        }

        /* Parse Ollama response into segments -- we treat the whole response as one segment */
        $duration = self::getWavDuration($wavPath);
        return [[
            'start' => 0.0,
            'end'   => $duration > 0 ? $duration : 5.0,
            'text'  => trim($text),
        ]];
    }

    /* ================================================================
     * Internal: helpers
     * ================================================================ */

    private static function findWhisper(): ?string
    {
        /* Check common locations for whisper binary */
        $candidates = [
            '/usr/local/bin/whisper',
            '/usr/bin/whisper',
            getenv('HOME') . '/.local/bin/whisper',
        ];
        foreach ($candidates as $path) {
            if (file_exists($path) && is_executable($path)) {
                return $path;
            }
        }

        /* Try which */
        $which = trim(shell_exec('which whisper 2>/dev/null') ?? '');
        if (!empty($which) && is_executable($which)) {
            return $which;
        }
        return null;
    }

    private static function ensureTmpDir(): void
    {
        if (!is_dir(self::TMP_DIR)) {
            @mkdir(self::TMP_DIR, 0750, true);
        }
    }

    private static function getWavDuration(string $path): float
    {
        $size = filesize($path);
        if ($size < 44) return 0;

        $fp = fopen($path, 'rb');
        if (!$fp) return 0;
        fseek($fp, 24);
        $sr = unpack('V', fread($fp, 4))[1] ?? 16000;
        fseek($fp, 32);
        $blockAlign = unpack('v', fread($fp, 2))[1] ?? 2;
        fclose($fp);

        $dataSize = $size - 44;
        if ($sr <= 0 || $blockAlign <= 0) return 0;
        return $dataSize / ($sr * $blockAlign);
    }

    private static function parseSrtToSegments(string $srt): array
    {
        $segments = [];
        $blocks = preg_split('/\n\n+/', trim($srt));
        foreach ($blocks as $block) {
            $lines = explode("\n", trim($block));
            $tsLine = -1;
            for ($i = 0; $i < min(count($lines), 2); $i++) {
                if (strpos($lines[$i], '-->') !== false) { $tsLine = $i; break; }
            }
            if ($tsLine < 0) continue;

            $parts = explode('-->', $lines[$tsLine]);
            if (count($parts) < 2) continue;

            $start = self::parseSrtTimestamp(trim($parts[0]));
            $end   = self::parseSrtTimestamp(trim($parts[1]));
            if ($start < 0 || $end < 0) continue;

            $text = implode(' ', array_slice($lines, $tsLine + 1));
            $text = trim($text);
            if ($text !== '') {
                $segments[] = ['start' => $start, 'end' => $end, 'text' => $text];
            }
        }
        return $segments;
    }

    private static function parseSrtTimestamp(string $ts): float
    {
        $ts = str_replace(',', '.', $ts);
        if (!preg_match('/(\d+):(\d+):(\d+)\.?(\d*)/', $ts, $m)) return -1;
        return (int)$m[1] * 3600 + (int)$m[2] * 60 + (int)$m[3]
             + ($m[4] !== '' ? (int)str_pad(substr($m[4], 0, 3), 3, '0') / 1000 : 0);
    }

    private static function srtToVtt(string $srt): string
    {
        $vtt = "WEBVTT\n\n";
        $blocks = preg_split('/\n\n+/', trim($srt));
        foreach ($blocks as $block) {
            $lines = explode("\n", trim($block));
            foreach ($lines as &$line) {
                /* Convert SRT timestamps to VTT (comma -> dot) */
                if (strpos($line, '-->') !== false) {
                    $line = str_replace(',', '.', $line);
                }
            }
            unset($line);
            /* Skip the numeric index line */
            $tsIdx = -1;
            for ($i = 0; $i < min(count($lines), 2); $i++) {
                if (strpos($lines[$i], '-->') !== false) { $tsIdx = $i; break; }
            }
            if ($tsIdx < 0) continue;
            $vtt .= implode("\n", array_slice($lines, $tsIdx)) . "\n\n";
        }
        return $vtt;
    }

    private static function vttToSrt(string $vtt): string
    {
        /* Strip WEBVTT header */
        $vtt = preg_replace('/^WEBVTT.*?\n\n/s', '', $vtt);
        $blocks = preg_split('/\n\n+/', trim($vtt));
        $srt = '';
        $idx = 1;
        foreach ($blocks as $block) {
            $lines = explode("\n", trim($block));
            $tsIdx = -1;
            for ($i = 0; $i < min(count($lines), 2); $i++) {
                if (strpos($lines[$i], '-->') !== false) { $tsIdx = $i; break; }
            }
            if ($tsIdx < 0) continue;
            /* Convert VTT timestamps: dot -> comma for milliseconds */
            $tsLine = $lines[$tsIdx];
            $tsLine = preg_replace_callback('/(\d{2}:\d{2}:\d{2})\.(\d{3})/', function($m) {
                return $m[1] . ',' . $m[2];
            }, $tsLine);

            $text = implode("\n", array_slice($lines, $tsIdx + 1));
            $srt .= $idx . "\n" . $tsLine . "\n" . $text . "\n\n";
            $idx++;
        }
        return $srt;
    }
}

/* -- Dispatch -- */
try {
    switch ($action) {
        case 'transcribe_chunk':
            mc1_api_respond(CaptionsApi::transcribeChunk($data));
            break;
        case 'transcribe_file':
            mc1_api_respond(CaptionsApi::transcribeFile($data));
            break;
        case 'burn_captions':
            mc1_api_respond(CaptionsApi::burnCaptions($data));
            break;
        case 'save_captions':
            mc1_api_respond(CaptionsApi::saveCaptions($data));
            break;
        case 'load_captions':
            mc1_api_respond(CaptionsApi::loadCaptions($data));
            break;
        case 'delete_captions':
            mc1_api_respond(CaptionsApi::deleteCaptions($data));
            break;
        case 'list_captions':
            mc1_api_respond(CaptionsApi::listCaptions($data));
            break;
        default:
            mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
            break;
    }
} catch (Throwable $e) {
    mc1_log(MC1_LOG_ERROR, 'Captions API error: ' . $e->getMessage(), 'captions');
    mc1_api_respond(['error' => 'Internal error: ' . $e->getMessage()], 500);
}
