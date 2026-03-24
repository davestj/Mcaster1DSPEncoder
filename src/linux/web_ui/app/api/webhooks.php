<?php
/**
 * webhooks.php — Webhook Management API
 *
 * File:    src/linux/web_ui/app/api/webhooks.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We manage webhook configurations for social integrations (Discord, Slack,
 *          custom HTTP). We handle CRUD and test/fire operations for Phase L11.
 *
 * Actions (all require admin auth):
 *  list    — list all webhook configs
 *  create  — create a new webhook config
 *  update  — update an existing webhook config
 *  delete  — delete a webhook config
 *  test    — send a test payload to the webhook URL
 *  fire    — internal: fire webhook for a specific event
 *
 * Standards:
 *  - We never call exit() or die() — uopz extension is active
 *  - We use Mc1Db trait for all database access
 *  - We use first-person plural in all comments
 *  - We use mc1_api_respond() for all JSON responses
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/../inc/mc1_config.php';
require_once __DIR__ . '/../inc/db.php';
require_once __DIR__ . '/../inc/traits.db.class.php';
require_once __DIR__ . '/../inc/logger.php';
require_once __DIR__ . '/../inc/auth.php';
require_once __DIR__ . '/../inc/user_auth.php';

header('Content-Type: application/json');

/* ── Auth gate — all webhook operations require admin auth ── */
if (!mc1_is_authed()) {
    mc1_api_respond(['error' => 'Unauthorized'], 403);
    return;
}

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    mc1_api_respond(['error' => 'POST required'], 405);
    return;
}

$input  = json_decode(file_get_contents('php://input'), true) ?: [];
$action = $input['action'] ?? '';

class WebhookManager {
    use Mc1Db;

    private static $validServices = ['discord', 'slack', 'twitter', 'custom'];
    private static $validEvents   = ['now_playing', 'listener_count', 'request_received'];

    /**
     * We list all webhook configurations.
     */
    public static function list(): array
    {
        $rows = self::rows('mcaster1_encoder', 'SELECT * FROM webhook_configs ORDER BY created_at DESC');
        return ['ok' => true, 'webhooks' => $rows];
    }

    /**
     * We create a new webhook configuration.
     */
    public static function create(array $input): array
    {
        $name     = trim($input['name'] ?? '');
        $service  = $input['service'] ?? '';
        $url      = trim($input['webhook_url'] ?? '');
        $events   = $input['events'] ?? 'now_playing';
        $template = trim($input['template'] ?? '') ?: null;
        $active   = (bool) ($input['is_active'] ?? true);

        if ($name === '') return ['ok' => false, 'error' => 'Webhook name is required.'];
        if (!in_array($service, self::$validServices)) return ['ok' => false, 'error' => 'Invalid service type.'];
        if ($url === '' || !filter_var($url, FILTER_VALIDATE_URL)) return ['ok' => false, 'error' => 'A valid webhook URL is required.'];

        /* We validate and clean the events list */
        if (is_array($events)) {
            $events = implode(',', array_filter($events, function ($e) {
                return in_array($e, ['now_playing', 'listener_count', 'request_received']);
            }));
        }
        if ($events === '') $events = 'now_playing';

        self::run(
            'mcaster1_encoder',
            'INSERT INTO webhook_configs (name, service, webhook_url, events, is_active, template) VALUES (?, ?, ?, ?, ?, ?)',
            [$name, $service, $url, $events, $active ? 1 : 0, $template]
        );

        return ['ok' => true, 'id' => (int) self::lastId('mcaster1_encoder')];
    }

    /**
     * We update an existing webhook configuration.
     */
    public static function update(array $input): array
    {
        $id       = (int) ($input['id'] ?? 0);
        $name     = trim($input['name'] ?? '');
        $service  = $input['service'] ?? '';
        $url      = trim($input['webhook_url'] ?? '');
        $events   = $input['events'] ?? 'now_playing';
        $template = trim($input['template'] ?? '') ?: null;
        $active   = (bool) ($input['is_active'] ?? true);

        if ($id < 1) return ['ok' => false, 'error' => 'Invalid webhook ID.'];
        if ($name === '') return ['ok' => false, 'error' => 'Webhook name is required.'];
        if (!in_array($service, self::$validServices)) return ['ok' => false, 'error' => 'Invalid service type.'];
        if ($url === '' || !filter_var($url, FILTER_VALIDATE_URL)) return ['ok' => false, 'error' => 'A valid webhook URL is required.'];

        if (is_array($events)) {
            $events = implode(',', array_filter($events, function ($e) {
                return in_array($e, ['now_playing', 'listener_count', 'request_received']);
            }));
        }
        if ($events === '') $events = 'now_playing';

        self::run(
            'mcaster1_encoder',
            'UPDATE webhook_configs SET name = ?, service = ?, webhook_url = ?, events = ?, is_active = ?, template = ? WHERE id = ?',
            [$name, $service, $url, $events, $active ? 1 : 0, $template, $id]
        );

        return ['ok' => true];
    }

    /**
     * We delete a webhook configuration.
     */
    public static function delete(array $input): array
    {
        $id = (int) ($input['id'] ?? 0);
        if ($id < 1) return ['ok' => false, 'error' => 'Invalid webhook ID.'];
        self::run('mcaster1_encoder', 'DELETE FROM webhook_configs WHERE id = ?', [$id]);
        return ['ok' => true];
    }

    /**
     * We send a test payload to the webhook URL.
     */
    public static function test(array $input): array
    {
        $id = (int) ($input['id'] ?? 0);
        if ($id < 1) return ['ok' => false, 'error' => 'Invalid webhook ID.'];

        $hook = self::row('mcaster1_encoder', 'SELECT * FROM webhook_configs WHERE id = ?', [$id]);
        if (!$hook) return ['ok' => false, 'error' => 'Webhook not found.'];

        $testData = [
            'title'     => 'Test Track - Hotel California',
            'artist'    => 'Eagles',
            'listeners' => 42,
            'mount'     => '/yolo-rock',
            'slot'      => 1,
        ];

        $result = self::sendWebhook($hook, $testData);
        return $result;
    }

    /**
     * We fire webhooks for a specific event. This is the internal dispatch
     * called when events occur (now_playing, listener_count, request_received).
     */
    public static function fire(array $input): array
    {
        $event = $input['event'] ?? '';
        $data  = $input['data'] ?? [];

        if (!in_array($event, self::$validEvents)) {
            return ['ok' => false, 'error' => 'Invalid event type.'];
        }

        /* We find all active webhooks that subscribe to this event */
        $hooks = self::rows(
            'mcaster1_encoder',
            'SELECT * FROM webhook_configs WHERE is_active = 1'
        );

        $fired  = 0;
        $errors = [];
        foreach ($hooks as $hook) {
            $hookEvents = array_map('trim', explode(',', $hook['events']));
            if (!in_array($event, $hookEvents)) continue;

            $result = self::sendWebhook($hook, $data);
            if ($result['ok']) {
                $fired++;
                self::run('mcaster1_encoder', 'UPDATE webhook_configs SET last_fired_at = NOW() WHERE id = ?', [(int) $hook['id']]);
            } else {
                $errors[] = $hook['name'] . ': ' . ($result['error'] ?? 'unknown error');
            }
        }

        return ['ok' => true, 'fired' => $fired, 'errors' => $errors];
    }

    /**
     * We send an HTTP POST to the webhook URL with the appropriate payload
     * format based on the service type.
     */
    private static function sendWebhook(array $hook, array $data): array
    {
        $service  = $hook['service'];
        $url      = $hook['webhook_url'];
        $template = $hook['template'] ?? null;

        /* We build the message by replacing placeholders */
        $defaultMsg = 'Now Playing: ' . ($data['title'] ?? 'Unknown') . ' by ' . ($data['artist'] ?? 'Unknown');

        if ($template) {
            $msg = str_replace(
                ['{title}', '{artist}', '{listeners}', '{mount}', '{slot}'],
                [
                    $data['title'] ?? 'Unknown',
                    $data['artist'] ?? 'Unknown',
                    $data['listeners'] ?? 0,
                    $data['mount'] ?? '',
                    $data['slot'] ?? '',
                ],
                $template
            );
        } else {
            $msg = $defaultMsg;
        }

        /* We construct the payload based on service type */
        switch ($service) {
            case 'discord':
                $payload = json_encode(['content' => $msg]);
                break;
            case 'slack':
                $payload = json_encode(['text' => $msg]);
                break;
            case 'twitter':
            case 'custom':
            default:
                $payload = json_encode([
                    'message'   => $msg,
                    'title'     => $data['title'] ?? null,
                    'artist'    => $data['artist'] ?? null,
                    'listeners' => $data['listeners'] ?? null,
                    'mount'     => $data['mount'] ?? null,
                    'slot'      => $data['slot'] ?? null,
                ]);
                break;
        }

        /* We use file_get_contents with a stream context for the HTTP POST */
        $opts = [
            'http' => [
                'method'  => 'POST',
                'header'  => "Content-Type: application/json\r\nUser-Agent: Mcaster1DSPEncoder/1.4.0\r\n",
                'content' => $payload,
                'timeout' => 10,
                'ignore_errors' => true,
            ],
            'ssl' => [
                'verify_peer'      => true,
                'verify_peer_name' => true,
            ],
        ];

        $context  = stream_context_create($opts);
        $response = @file_get_contents($url, false, $context);

        if ($response === false) {
            $err = error_get_last();
            mc1_log(MC1_LOG_WARN, 'Webhook send failed to ' . $url . ': ' . ($err['message'] ?? 'unknown'), 'webhooks');
            return ['ok' => false, 'error' => 'Failed to connect to webhook URL: ' . ($err['message'] ?? 'unknown error')];
        }

        /* We check the HTTP status from $http_response_header */
        $httpCode = 0;
        if (isset($http_response_header) && is_array($http_response_header)) {
            foreach ($http_response_header as $hdr) {
                if (preg_match('/^HTTP\/[\d.]+ (\d+)/', $hdr, $m)) {
                    $httpCode = (int) $m[1];
                }
            }
        }

        if ($httpCode >= 200 && $httpCode < 300) {
            mc1_log(MC1_LOG_INFO, 'Webhook fired successfully to ' . $url . ' (HTTP ' . $httpCode . ')', 'webhooks');
            return ['ok' => true, 'http_code' => $httpCode];
        }

        mc1_log(MC1_LOG_WARN, 'Webhook returned HTTP ' . $httpCode . ' from ' . $url, 'webhooks');
        return ['ok' => false, 'error' => 'Webhook returned HTTP ' . $httpCode, 'http_code' => $httpCode];
    }
}

/* ── We route actions ── */
try {
    switch ($action) {
        case 'list':
            mc1_api_respond(WebhookManager::list());
            return;
        case 'create':
            mc1_api_respond(WebhookManager::create($input));
            return;
        case 'update':
            mc1_api_respond(WebhookManager::update($input));
            return;
        case 'delete':
            mc1_api_respond(WebhookManager::delete($input));
            return;
        case 'test':
            mc1_api_respond(WebhookManager::test($input));
            return;
        case 'fire':
            mc1_api_respond(WebhookManager::fire($input));
            return;
        default:
            mc1_api_respond(['error' => 'Unknown action: ' . $action], 400);
            return;
    }
} catch (\Exception $e) {
    mc1_log(MC1_LOG_ERROR, 'webhooks.php error: ' . $e->getMessage(), 'webhooks');
    mc1_api_respond(['error' => mc1_safe_error($e, 'Webhook operation failed')], 500);
    return;
}
