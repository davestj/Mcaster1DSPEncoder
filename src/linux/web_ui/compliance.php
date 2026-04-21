<?php
/**
 * compliance.php — Security Compliance Dashboard
 * Displays SAST/DAST reports, patch history, and security posture
 */
define('MC1_BOOT', true);
$page_title  = 'Compliance';
$active_nav  = 'compliance';
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/user_auth.php';
if (!mc1_is_authed()) { header('Location: /login.html'); return; }

/* Check both possible report locations */
/* __DIR__ = src/linux/web_ui — project root is 3 levels up */
$docs_dir = realpath(__DIR__ . '/../../../docs');
$sec_dir  = realpath(__DIR__ . '/../../../security');
if (!$docs_dir) $docs_dir = realpath(__DIR__ . '/../../docs'); /* fallback */

/* Map report names to actual file paths (docs/ has the current reports) */
$report_files = [
    'sast-report.html'           => $docs_dir . '/sast-report.html',
    'dast-report.html'           => $docs_dir . '/dast-report.html',
    'security-patch-status.html' => $docs_dir . '/security-patch-status.html',
];
/* Legacy names still supported */
if ($sec_dir) {
    foreach (['patchlist.html', 'fixed.html'] as $legacy) {
        $lp = $sec_dir . '/' . $legacy;
        if (file_exists($lp)) $report_files[$legacy] = $lp;
    }
}

$view = basename($_GET['view'] ?? '');
if ($view !== '' && isset($report_files[$view]) && file_exists($report_files[$view])) {
    /* Serve report in frameless popup — no header/footer, no print */
    header('Content-Type: text/html; charset=utf-8');
    echo '<!DOCTYPE html><html><head><meta charset="UTF-8"><title>' . htmlspecialchars($view) . '</title>';
    echo '<style>@media print { body { display:none !important; } body::after { content:"Report printing disabled"; display:block; font-size:24px; padding:40px; } }</style>';
    echo '</head><body>';
    readfile($report_files[$view]);
    echo '</body></html>';
    return;
}

require_once __DIR__ . '/app/inc/header.php';
$reports  = [];
foreach ($report_files as $f => $path) {
    $reports[$f] = [
        'exists' => file_exists($path),
        'size'   => file_exists($path) ? filesize($path) : 0,
        'mtime'  => file_exists($path) ? filemtime($path) : 0,
    ];
}
$tests = $sec_dir ? glob($sec_dir . '/testharness/sast-*.sh') : [];
$dast_tests = $sec_dir ? glob($sec_dir . '/testharness/dast-*.sh') : [];
?>

<style>
.comp-hero {
    background: linear-gradient(135deg, #064e3b 0%, #0f172a 100%);
    border-radius: var(--radius);
    padding: 2rem 2.5rem;
    margin-bottom: 1.5rem;
    display: flex;
    align-items: center;
    gap: 1.5rem;
}
.comp-shield {
    width: 72px;
    height: 72px;
    flex-shrink: 0;
}
.comp-hero h1 { color: #5eead4; font-size: 1.6rem; font-weight: 800; margin: 0 0 .35rem; }
.comp-hero p { color: #94a3b8; font-size: .9rem; margin: 0; line-height: 1.5; }
.comp-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
    gap: 1rem;
    margin-bottom: 1.5rem;
}
.comp-card {
    background: var(--card-bg, #1e293b);
    border: 1px solid var(--border, #334155);
    border-radius: var(--radius, 8px);
    padding: 1.5rem;
    transition: all .2s;
}
.comp-card:hover { border-color: #00d4aa; transform: translateY(-2px); }
.comp-card h3 { font-size: .95rem; font-weight: 700; margin: 0 0 .5rem; display: flex; align-items: center; gap: .5rem; }
.comp-card h3 svg { width: 20px; height: 20px; }
.comp-card p { font-size: .82rem; color: var(--text-dim, #94a3b8); margin: 0 0 1rem; line-height: 1.5; }
.comp-card .meta { font-size: .75rem; color: var(--text-dim, #64748b); }
.comp-btn {
    display: inline-flex;
    align-items: center;
    gap: .4rem;
    padding: .45rem 1rem;
    background: linear-gradient(135deg, #00d4aa, #06b6d4);
    color: #0a0e1a;
    border: none;
    border-radius: 6px;
    font-weight: 700;
    font-size: .8rem;
    text-decoration: none;
    cursor: pointer;
    transition: all .2s;
}
.comp-btn:hover { transform: translateY(-1px); box-shadow: 0 4px 12px rgba(0,212,170,.3); }
.comp-btn-sec {
    background: transparent;
    border: 1px solid var(--border, #334155);
    color: var(--text, #e2e8f0);
}
.comp-btn-sec:hover { border-color: #00d4aa; }
.comp-section {
    background: var(--card-bg, #1e293b);
    border: 1px solid var(--border, #334155);
    border-radius: var(--radius, 8px);
    padding: 1.5rem 2rem;
    margin-bottom: 1.5rem;
}
.comp-section h2 {
    font-size: 1.15rem;
    font-weight: 700;
    margin: 0 0 1rem;
    display: flex;
    align-items: center;
    gap: .6rem;
}
.comp-section h2 i { color: #00d4aa; }
.comp-table { width: 100%; border-collapse: collapse; font-size: .82rem; }
.comp-table th { text-align: left; padding: .5rem .75rem; color: var(--text-dim, #94a3b8); font-size: .75rem; text-transform: uppercase; border-bottom: 1px solid var(--border, #334155); }
.comp-table td { padding: .5rem .75rem; border-bottom: 1px solid var(--border, #1e293b); }
.badge-ok { background: #064e3b; color: #6ee7b7; padding: .15rem .5rem; border-radius: 4px; font-size: .72rem; font-weight: 700; }
.badge-warn { background: #713f12; color: #fde68a; padding: .15rem .5rem; border-radius: 4px; font-size: .72rem; font-weight: 700; }
.badge-miss { background: #7f1d1d; color: #fca5a5; padding: .15rem .5rem; border-radius: 4px; font-size: .72rem; font-weight: 700; }
.comp-export-bar {
    display: flex;
    gap: .75rem;
    margin-bottom: 1.5rem;
    flex-wrap: wrap;
}
.comp-export-bar .comp-btn svg { width: 16px; height: 16px; }
@media print {
    .comp-export-bar, .sidebar, .nav, header { display: none !important; }
    .comp-hero { break-inside: avoid; }
    .comp-card { break-inside: avoid; }
}
</style>

<!-- Hero -->
<div class="comp-hero">
    <svg class="comp-shield" viewBox="0 0 72 72" fill="none" xmlns="http://www.w3.org/2000/svg">
        <defs>
            <linearGradient id="shg1" x1="10" y1="8" x2="62" y2="64"><stop stop-color="#00d4aa"/><stop offset="1" stop-color="#06b6d4"/></linearGradient>
            <linearGradient id="shg2" x1="20" y1="20" x2="52" y2="55"><stop stop-color="#00d4aa" stop-opacity=".3"/><stop offset="1" stop-color="#06b6d4" stop-opacity=".1"/></linearGradient>
            <filter id="shglow"><feGaussianBlur stdDeviation="2" result="blur"/><feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
        </defs>
        <path d="M36 6L10 18v16c0 16 10 30 26 34 16-4 26-18 26-34V18L36 6z" fill="url(#shg2)" stroke="url(#shg1)" stroke-width="2.5" filter="url(#shglow)"/>
        <path d="M24 36l8 8 16-16" stroke="url(#shg1)" stroke-width="4" stroke-linecap="round" stroke-linejoin="round" filter="url(#shglow)"/>
        <circle cx="36" cy="36" r="2" fill="#00d4aa" opacity=".5"/>
    </svg>
    <div>
        <h1>Security Compliance</h1>
        <p>SAST/DAST scan results, patch history, and test harness status for <?= h(basename(dirname($sec_dir))) ?></p>
    </div>
</div>

<!-- Export Actions -->
<div class="comp-export-bar">
    <button class="comp-btn" onclick="window.print()" title="Print or save as PDF via browser print dialog">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M6 9V2h12v7"/><path d="M6 18H4a2 2 0 01-2-2v-5a2 2 0 012-2h16a2 2 0 012 2v5a2 2 0 01-2 2h-2"/><rect x="6" y="14" width="12" height="8"/></svg>
        Print / Save as PDF
    </button>
    <button class="comp-btn comp-btn-sec" onclick="exportHTML()" title="Download this page as a standalone HTML file">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
        Export HTML
    </button>
</div>
<script>
function exportHTML() {
    var html = document.documentElement.outerHTML;
    var blob = new Blob(['<!DOCTYPE html>\n' + html], {type: 'text/html;charset=utf-8'});
    var url  = URL.createObjectURL(blob);
    var a    = document.createElement('a');
    a.href     = url;
    a.download = 'compliance-report-' + new Date().toISOString().slice(0,10) + '.html';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}
</script>

<!-- Report Cards -->
<div class="comp-grid">
    <?php
    $card_info = [
        'sast-report.html'           => ['SAST Report', 'Static analysis — SQL injection, XSS, auth bypass, CSRF, secrets, command injection, path traversal', 'fa-magnifying-glass-chart'],
        'dast-report.html'           => ['DAST Report', 'Dynamic analysis — HTTP headers, auth enforcement, injection probes, directory listing', 'fa-globe'],
        'security-patch-status.html' => ['Patch Status &amp; Fixed Issues', 'All security patches with severity, remediation details, and compliance status', 'fa-list-check'],
    ];
    foreach ($card_info as $file => [$title, $desc, $icon]) {
        $r = $reports[$file];
        $badge = $r['exists'] ? '<span class="badge-ok">Available</span>' : '<span class="badge-miss">Missing</span>';
        $date = $r['mtime'] ? date('Y-m-d H:i', $r['mtime']) : 'N/A';
        $size = $r['size'] ? number_format($r['size'] / 1024, 1) . ' KB' : '';
        echo '<div class="comp-card">';
        echo '<h3><i class="fas ' . $icon . '" style="color:#00d4aa"></i> ' . h($title) . ' ' . $badge . '</h3>';
        echo '<p>' . h($desc) . '</p>';
        if ($r['exists']) {
            echo '<a href="#" onclick="window.open(\'/compliance.php?view=' . h($file) . '\',\'mc1report\',\'width=1100,height=800,menubar=no,toolbar=no,location=yes,status=no,resizable=yes\');return false;" class="comp-btn"><i class="fas fa-external-link-alt"></i> View Report</a> ';
        }
        echo '<div class="meta" style="margin-top:.5rem">Updated: ' . h($date) . ' &bull; ' . h($size) . '</div>';
        echo '</div>';
    }
    ?>
</div>

<!-- Test Harness -->
<div class="comp-section">
    <h2><i class="fas fa-flask-vial"></i> Test Harness</h2>
    <table class="comp-table">
        <thead><tr><th>Test</th><th>Type</th><th>Description</th></tr></thead>
        <tbody>
        <?php
        $test_info = [
            'sast-sql-injection'     => ['SAST', 'Detects raw SQL string concatenation in PHP and C++'],
            'sast-xss'               => ['SAST', 'Detects unescaped output missing h() or htmlspecialchars()'],
            'sast-auth-bypass'       => ['SAST', 'Detects API endpoints missing auth checks'],
            'sast-csrf'              => ['SAST', 'Detects POST handlers without CSRF token validation'],
            'sast-secrets'           => ['SAST', 'Detects hardcoded passwords and API keys in source'],
            'sast-command-injection' => ['SAST', 'Detects shell execution with user-controlled input'],
            'sast-path-traversal'    => ['SAST', 'Detects file operations with user-controlled paths'],
            'dast-headers'           => ['DAST', 'Checks HTTP security headers on live endpoints'],
            'dast-auth-required'     => ['DAST', 'Verifies protected endpoints reject unauthenticated access'],
            'dast-injection-probes'  => ['DAST', 'Sends SQL/XSS/traversal payloads to live endpoints'],
            'dast-directory-listing' => ['DAST', 'Checks for directory listing and sensitive file exposure'],
        ];
        foreach ($test_info as $name => [$type, $desc]) {
            $badge = $type === 'SAST' ? 'badge-ok' : 'badge-warn';
            echo "<tr><td><code>{$name}</code></td><td><span class=\"{$badge}\">{$type}</span></td><td>{$desc}</td></tr>";
        }
        ?>
        </tbody>
    </table>
    <div style="margin-top:1rem;display:flex;gap:.75rem;flex-wrap:wrap">
        <span class="comp-btn-sec comp-btn" style="cursor:default"><i class="fas fa-terminal"></i> bash security/testharness/run-all.sh --ci</span>
    </div>
</div>

<!-- CI Pipelines -->
<div class="comp-section">
    <h2><i class="fas fa-rotate"></i> CI/CD Integration</h2>
    <table class="comp-table">
        <thead><tr><th>Platform</th><th>File</th><th>Status</th></tr></thead>
        <tbody>
        <?php
        $pipelines = [
            ['Jenkins', 'security/testharness/Jenkinsfile', $sec_dir . '/testharness/Jenkinsfile'],
            ['GitHub Actions', 'security/testharness/security-scan.yml', $sec_dir . '/testharness/security-scan.yml'],
            ['GitLab CI', 'security/testharness/.gitlab-ci-security.yml', $sec_dir . '/testharness/.gitlab-ci-security.yml'],
        ];
        foreach ($pipelines as [$platform, $rel, $abs]) {
            $exists = file_exists($abs);
            $badge = $exists ? '<span class="badge-ok">Ready</span>' : '<span class="badge-miss">Missing</span>';
            echo "<tr><td>{$platform}</td><td><code>{$rel}</code></td><td>{$badge}</td></tr>";
        }
        ?>
        </tbody>
    </table>
</div>

<?php require_once __DIR__ . '/app/inc/footer.php'; ?>
