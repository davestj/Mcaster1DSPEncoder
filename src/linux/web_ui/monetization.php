<?php
/**
 * monetization.php -- Podcast Monetization Dashboard
 *
 * File:    src/linux/web_ui/monetization.php
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Purpose: We provide a full monetization management interface with ad campaign CRUD,
 *          placement management, sponsor directory, revenue analytics, and ad performance.
 *
 * Standards:
 *  - We never call exit() or die() -- uopz extension is active
 *  - We wrap all startup JS in DOMContentLoaded
 *  - We use mc1Api() for all fetch calls (defined in footer.php)
 *  - We use h() for all user data rendered into HTML
 */

define('MC1_BOOT', true);
require_once __DIR__ . '/app/inc/mc1_config.php';
require_once __DIR__ . '/app/inc/db.php';
require_once __DIR__ . '/app/inc/traits.db.class.php';
require_once __DIR__ . '/app/inc/logger.php';
require_once __DIR__ . '/app/inc/auth.php';
require_once __DIR__ . '/app/inc/user_auth.php';

$page_title = 'Monetization';
$active_nav = 'monetization';
$use_charts = true;

require __DIR__ . '/app/inc/header.php';
?>

<style>
/* Monetization-specific styles */
.mon-tabs { display: flex; gap: 2px; margin-bottom: 18px; background: var(--bg3); border-radius: var(--radius); padding: 3px; overflow-x: auto; }
.mon-tab { padding: 8px 16px; border-radius: var(--radius-sm); font-size: 13px; font-weight: 500; color: var(--text-dim); cursor: pointer; white-space: nowrap; transition: all .15s; border: none; background: none; }
.mon-tab:hover { color: var(--text); background: rgba(255,255,255,.04); }
.mon-tab.active { background: var(--card); color: var(--teal); box-shadow: 0 1px 3px rgba(0,0,0,.2); }
.mon-panel { display: none; }
.mon-panel.active { display: block; }

/* Revenue overview cards */
.mon-overview { display: grid; grid-template-columns: repeat(4, 1fr); gap: 14px; margin-bottom: 18px; }
@media(max-width:860px) { .mon-overview { grid-template-columns: repeat(2, 1fr); } }
@media(max-width:580px) { .mon-overview { grid-template-columns: 1fr; } }

/* Campaign table */
.mon-tbl { width: 100%; border-collapse: collapse; font-size: 13px; }
.mon-tbl th { text-align: left; padding: 10px 12px; border-bottom: 2px solid var(--border); color: var(--muted); font-weight: 600; font-size: 11px; text-transform: uppercase; letter-spacing: .05em; }
.mon-tbl td { padding: 10px 12px; border-bottom: 1px solid var(--border); color: var(--text-dim); }
.mon-tbl tr:hover td { background: rgba(255,255,255,.02); }
.mon-tbl .type-badge { font-size: 10px; padding: 2px 8px; border-radius: 4px; font-weight: 600; }
.mon-tbl .type-badge.pre_roll { background: rgba(20,184,166,.12); color: var(--teal); }
.mon-tbl .type-badge.mid_roll { background: rgba(249,115,22,.12); color: var(--orange); }
.mon-tbl .type-badge.post_roll { background: rgba(139,92,246,.12); color: #8b5cf6; }
.mon-tbl .active-dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 6px; }
.mon-tbl .active-dot.on { background: var(--green); }
.mon-tbl .active-dot.off { background: var(--red); }

/* Sponsor cards */
.sp-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(280px, 1fr)); gap: 14px; }
.sp-card { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 16px; transition: border-color .15s; }
.sp-card:hover { border-color: var(--teal); }
.sp-hdr { display: flex; align-items: center; gap: 12px; margin-bottom: 10px; }
.sp-logo { width: 40px; height: 40px; border-radius: 8px; background: rgba(20,184,166,.1); display: flex; align-items: center; justify-content: center; font-size: 18px; color: var(--teal); flex-shrink: 0; overflow: hidden; }
.sp-logo img { width: 100%; height: 100%; object-fit: cover; }
.sp-name { font-weight: 600; font-size: 14px; color: var(--text); }
.sp-meta { font-size: 11px; color: var(--muted); }
.sp-acts { display: flex; gap: 4px; margin-top: 10px; }

/* Chart container */
.mon-chart-wrap { background: var(--card); border: 1px solid var(--border); border-radius: var(--radius); padding: 16px; margin-bottom: 14px; }
.mon-chart-wrap canvas { max-height: 280px; }

/* Modal */
.mon-modal { display: none; position: fixed; inset: 0; background: rgba(0,0,0,.6); z-index: 500; align-items: center; justify-content: center; }
.mon-modal.open { display: flex; }
.mon-modal-box { background: var(--bg2); border: 1px solid var(--border); border-radius: var(--radius); padding: 24px; width: 560px; max-width: 95vw; max-height: 85vh; overflow-y: auto; }
.mon-modal-title { font-size: 17px; font-weight: 700; color: var(--text); margin-bottom: 16px; }
.mon-modal-acts { display: flex; gap: 8px; justify-content: flex-end; margin-top: 18px; }

/* Performance table */
.perf-bar { height: 4px; background: var(--border); border-radius: 2px; overflow: hidden; min-width: 60px; }
.perf-bar-inner { height: 100%; background: var(--teal); border-radius: 2px; }
</style>

<!-- Revenue Overview Cards -->
<div class="mon-overview">
    <div class="stat-card">
        <div class="stat-icon teal"><i class="fa-solid fa-dollar-sign"></i></div>
        <div class="stat-label">Total Revenue</div>
        <div class="stat-value" id="monTotalRevenue">$0.00</div>
        <div class="stat-sub" id="monRevenuePeriod">Last 30 days</div>
    </div>
    <div class="stat-card">
        <div class="stat-icon cyan"><i class="fa-solid fa-eye"></i></div>
        <div class="stat-label">Impressions</div>
        <div class="stat-value" id="monTotalImpressions">0</div>
        <div class="stat-sub" id="monImpressionsPeriod">Last 30 days</div>
    </div>
    <div class="stat-card">
        <div class="stat-icon green"><i class="fa-solid fa-hand-pointer"></i></div>
        <div class="stat-label">Avg CPM</div>
        <div class="stat-value" id="monAvgCpm">$0.00</div>
        <div class="stat-sub">Across all campaigns</div>
    </div>
    <div class="stat-card">
        <div class="stat-icon orange"><i class="fa-solid fa-bullhorn"></i></div>
        <div class="stat-label">Active Campaigns</div>
        <div class="stat-value" id="monActiveCampaigns">0</div>
        <div class="stat-sub" id="monTotalCampaigns">0 total</div>
    </div>
</div>

<!-- Tabs -->
<div class="mon-tabs" id="monTabs">
    <button class="mon-tab active" data-tab="campaigns">Campaigns</button>
    <button class="mon-tab" data-tab="sponsors">Sponsors</button>
    <button class="mon-tab" data-tab="revenue">Revenue</button>
    <button class="mon-tab" data-tab="performance">Performance</button>
</div>

<!-- Campaigns Tab -->
<div class="mon-panel active" id="panel-campaigns">
    <div class="sec-hdr">
        <div class="sec-title">Ad Campaigns</div>
        <button class="btn btn-primary" onclick="openCampaignModal()">
            <i class="fa-solid fa-plus"></i> New Campaign
        </button>
    </div>
    <div class="card" style="overflow-x:auto">
        <table class="mon-tbl" id="campaignTable">
            <thead>
                <tr>
                    <th>Name</th>
                    <th>Advertiser</th>
                    <th>Type</th>
                    <th>CPM</th>
                    <th>Impressions</th>
                    <th>Clicks</th>
                    <th>Revenue</th>
                    <th>Status</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody id="campaignTableBody">
                <tr><td colspan="9" style="text-align:center;padding:40px;color:var(--muted)">Loading...</td></tr>
            </tbody>
        </table>
    </div>
</div>

<!-- Sponsors Tab -->
<div class="mon-panel" id="panel-sponsors">
    <div class="sec-hdr">
        <div class="sec-title">Sponsor Directory</div>
        <button class="btn btn-primary" onclick="openSponsorModal()">
            <i class="fa-solid fa-plus"></i> New Sponsor
        </button>
    </div>
    <div class="sp-grid" id="sponsorGrid">
        <div style="text-align:center;padding:40px;color:var(--muted);grid-column:1/-1">Loading...</div>
    </div>
</div>

<!-- Revenue Tab -->
<div class="mon-panel" id="panel-revenue">
    <div class="sec-hdr">
        <div class="sec-title">Revenue Over Time</div>
        <div style="display:flex;gap:8px">
            <select id="revPeriod" class="form-control" style="width:auto" onchange="loadRevenue()">
                <option value="daily">Daily</option>
                <option value="weekly">Weekly</option>
                <option value="monthly" selected>Monthly</option>
            </select>
            <select id="revDays" class="form-control" style="width:auto" onchange="loadRevenue()">
                <option value="30">30 days</option>
                <option value="90">90 days</option>
                <option value="180">180 days</option>
                <option value="365" selected>1 year</option>
            </select>
        </div>
    </div>
    <div class="mon-chart-wrap">
        <canvas id="revenueChart"></canvas>
    </div>
</div>

<!-- Performance Tab -->
<div class="mon-panel" id="panel-performance">
    <div class="sec-hdr">
        <div class="sec-title">Ad Performance</div>
        <select id="perfDays" class="form-control" style="width:auto" onchange="loadPerformance()">
            <option value="7">Last 7 days</option>
            <option value="30" selected>Last 30 days</option>
            <option value="90">Last 90 days</option>
        </select>
    </div>
    <div class="card" style="overflow-x:auto">
        <table class="mon-tbl" id="perfTable">
            <thead>
                <tr>
                    <th>Campaign</th>
                    <th>Type</th>
                    <th>Impressions</th>
                    <th>Clicks</th>
                    <th>CTR</th>
                    <th>Revenue</th>
                    <th>Fill Rate</th>
                </tr>
            </thead>
            <tbody id="perfTableBody">
                <tr><td colspan="7" style="text-align:center;padding:40px;color:var(--muted)">Loading...</td></tr>
            </tbody>
        </table>
    </div>
</div>

<!-- Campaign Modal -->
<div class="mon-modal" id="campaignModal">
    <div class="mon-modal-box">
        <div class="mon-modal-title" id="campaignModalTitle">New Campaign</div>
        <input type="hidden" id="campId" value="0">

        <div class="form-group">
            <label class="form-label">Campaign Name</label>
            <input class="form-input" id="campName" placeholder="e.g. Acme Q1 Pre-Roll">
        </div>
        <div class="form-group">
            <label class="form-label">Advertiser</label>
            <input class="form-input" id="campAdvertiser" placeholder="Company name">
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
            <div class="form-group">
                <label class="form-label">Type</label>
                <select class="form-select" id="campType">
                    <option value="pre_roll">Pre-Roll</option>
                    <option value="mid_roll">Mid-Roll</option>
                    <option value="post_roll">Post-Roll</option>
                </select>
            </div>
            <div class="form-group">
                <label class="form-label">CPM Rate ($)</label>
                <input class="form-input" id="campCpm" type="number" step="0.01" min="0" value="0.00">
            </div>
        </div>
        <div class="form-group">
            <label class="form-label">Audio File Path</label>
            <div style="display:flex;gap:8px">
                <input class="form-input" id="campAudioPath" placeholder="/path/to/ad.mp3" style="flex:1">
                <label class="btn btn-secondary" style="cursor:pointer">
                    <i class="fa-solid fa-upload"></i> Upload
                    <input type="file" id="campAudioUpload" accept="audio/*" style="display:none" onchange="uploadAdAudio(this)">
                </label>
            </div>
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
            <div class="form-group">
                <label class="form-label">Duration (sec)</label>
                <input class="form-input" id="campDuration" type="number" min="1" value="30">
            </div>
            <div class="form-group">
                <label class="form-label">Max Impressions</label>
                <input class="form-input" id="campMaxImpressions" type="number" min="0" value="" placeholder="Unlimited">
            </div>
        </div>
        <div class="form-group">
            <label class="form-label">Click URL</label>
            <input class="form-input" id="campClickUrl" placeholder="https://advertiser.com/landing">
        </div>
        <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px">
            <div class="form-group">
                <label class="form-label">Start Date</label>
                <input class="form-input" id="campStartDate" type="date">
            </div>
            <div class="form-group">
                <label class="form-label">End Date</label>
                <input class="form-input" id="campEndDate" type="date">
            </div>
        </div>
        <div class="form-group">
            <label class="form-label">Targeting (JSON)</label>
            <textarea class="form-textarea" id="campTargeting" rows="2" placeholder='{"shows":[1,2],"geo":["US","CA"]}'></textarea>
            <span class="form-hint">Optional. Filter by show IDs, geo, categories.</span>
        </div>
        <div class="form-group">
            <label class="toggle-wrap">
                <label class="toggle">
                    <input type="checkbox" id="campActive" checked>
                    <span class="toggle-slider"></span>
                </label>
                Active
            </label>
        </div>

        <div class="mon-modal-acts">
            <button class="btn btn-secondary" onclick="closeCampaignModal()">Cancel</button>
            <button class="btn btn-primary" onclick="saveCampaign()">
                <i class="fa-solid fa-floppy-disk"></i> Save Campaign
            </button>
        </div>
    </div>
</div>

<!-- Sponsor Modal -->
<div class="mon-modal" id="sponsorModal">
    <div class="mon-modal-box">
        <div class="mon-modal-title" id="sponsorModalTitle">New Sponsor</div>
        <input type="hidden" id="spId" value="0">

        <div class="form-group">
            <label class="form-label">Sponsor Name</label>
            <input class="form-input" id="spName" placeholder="Company name">
        </div>
        <div class="form-group">
            <label class="form-label">Contact Email</label>
            <input class="form-input" id="spEmail" type="email" placeholder="contact@sponsor.com">
        </div>
        <div class="form-group">
            <label class="form-label">Website URL</label>
            <input class="form-input" id="spWebsite" placeholder="https://sponsor.com">
        </div>
        <div class="form-group">
            <label class="form-label">Logo Path</label>
            <input class="form-input" id="spLogo" placeholder="/path/to/logo.png">
        </div>
        <div class="form-group">
            <label class="form-label">Notes</label>
            <textarea class="form-textarea" id="spNotes" rows="3" placeholder="Internal notes about this sponsor..."></textarea>
        </div>
        <div class="form-group">
            <label class="toggle-wrap">
                <label class="toggle">
                    <input type="checkbox" id="spActive" checked>
                    <span class="toggle-slider"></span>
                </label>
                Active
            </label>
        </div>

        <div class="mon-modal-acts">
            <button class="btn btn-secondary" onclick="closeSponsorModal()">Cancel</button>
            <button class="btn btn-primary" onclick="saveSponsor()">
                <i class="fa-solid fa-floppy-disk"></i> Save Sponsor
            </button>
        </div>
    </div>
</div>

<script>
/* ── Monetization Dashboard JS ────────────────────────────────────────── */

var revenueChart = null;

function esc(s) {
    return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/'/g,'&#39;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function fmtMoney(n) {
    return '$' + parseFloat(n || 0).toFixed(2);
}

function fmtNum(n) {
    return parseInt(n || 0).toLocaleString();
}

/* ── Tabs ── */

function switchTab(tab) {
    document.querySelectorAll('.mon-tab').forEach(function(t) { t.classList.remove('active'); });
    document.querySelectorAll('.mon-panel').forEach(function(p) { p.classList.remove('active'); });
    document.querySelector('.mon-tab[data-tab="' + tab + '"]').classList.add('active');
    document.getElementById('panel-' + tab).classList.add('active');

    if (tab === 'revenue') loadRevenue();
    if (tab === 'performance') loadPerformance();
}

/* ── Load overview stats ── */

function loadOverview() {
    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({action:'revenue_summary', days:30, period:'daily'})}).then(function(d) {
        if (!d || !d.ok) return;
        var t = d.totals || {};
        document.getElementById('monTotalRevenue').textContent = fmtMoney(t.total_revenue);
        document.getElementById('monTotalImpressions').textContent = fmtNum(t.total_impressions);
        document.getElementById('monActiveCampaigns').textContent = fmtNum(t.active_campaigns);
        var imp = parseInt(t.total_impressions || 0);
        var rev = parseFloat(t.total_revenue || 0);
        document.getElementById('monAvgCpm').textContent = imp > 0 ? fmtMoney(rev / imp * 1000) : '$0.00';
    });

    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({action:'list_campaigns'})}).then(function(d) {
        if (d && d.ok && d.campaigns) {
            document.getElementById('monTotalCampaigns').textContent = d.campaigns.length + ' total';
        }
    });
}

/* ── Campaigns ── */

function loadCampaigns() {
    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({action:'list_campaigns'})}).then(function(d) {
        var body = document.getElementById('campaignTableBody');
        if (!d || !d.ok || !d.campaigns || d.campaigns.length === 0) {
            body.innerHTML = '<tr><td colspan="9" style="text-align:center;padding:40px;color:var(--muted)"><i class="fa-solid fa-bullhorn" style="font-size:32px;display:block;margin-bottom:10px;color:var(--border)"></i>No campaigns yet. Create your first ad campaign.</td></tr>';
            return;
        }

        var html = '';
        d.campaigns.forEach(function(c) {
            var imp = parseInt(c.total_impressions || c.impressions || 0);
            var clicks = parseInt(c.total_clicks || c.clicks || 0);
            var revenue = (imp / 1000 * parseFloat(c.cpm_rate || 0)).toFixed(2);
            var active = parseInt(c.is_active);

            html += '<tr>';
            html += '<td><strong>' + esc(c.name) + '</strong></td>';
            html += '<td>' + esc(c.advertiser || '--') + '</td>';
            html += '<td><span class="type-badge ' + esc(c.type) + '">' + esc(c.type.replace('_', '-')) + '</span></td>';
            html += '<td>' + fmtMoney(c.cpm_rate) + '</td>';
            html += '<td>' + fmtNum(imp) + '</td>';
            html += '<td>' + fmtNum(clicks) + '</td>';
            html += '<td><strong>' + fmtMoney(revenue) + '</strong></td>';
            html += '<td><span class="active-dot ' + (active ? 'on' : 'off') + '"></span>' + (active ? 'Active' : 'Inactive') + '</td>';
            html += '<td><div style="display:flex;gap:4px">';
            html += '<button class="btn btn-xs btn-secondary" onclick="editCampaign(' + c.id + ')" title="Edit"><i class="fa-solid fa-pen"></i></button>';
            html += '<button class="btn btn-xs btn-secondary" onclick="deleteCampaign(' + c.id + ',' + esc(JSON.stringify(c.name)) + ')" title="Delete"><i class="fa-solid fa-trash"></i></button>';
            html += '</div></td>';
            html += '</tr>';
        });
        body.innerHTML = html;
    });
}

function openCampaignModal(data) {
    document.getElementById('campId').value = (data && data.id) ? data.id : 0;
    document.getElementById('campaignModalTitle').textContent = (data && data.id) ? 'Edit Campaign' : 'New Campaign';
    document.getElementById('campName').value = (data && data.name) || '';
    document.getElementById('campAdvertiser').value = (data && data.advertiser) || '';
    document.getElementById('campType').value = (data && data.type) || 'pre_roll';
    document.getElementById('campCpm').value = (data && data.cpm_rate) || '0.00';
    document.getElementById('campAudioPath').value = (data && data.audio_file_path) || '';
    document.getElementById('campDuration').value = (data && data.duration_sec) || 30;
    document.getElementById('campMaxImpressions').value = (data && data.max_impressions) || '';
    document.getElementById('campClickUrl').value = (data && data.click_url) || '';
    document.getElementById('campStartDate').value = (data && data.start_date) || '';
    document.getElementById('campEndDate').value = (data && data.end_date) || '';
    document.getElementById('campTargeting').value = (data && data.targeting_json) || '';
    document.getElementById('campActive').checked = data ? !!parseInt(data.is_active) : true;
    document.getElementById('campaignModal').classList.add('open');
}

function closeCampaignModal() {
    document.getElementById('campaignModal').classList.remove('open');
}

function editCampaign(id) {
    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({action:'list_campaigns'})}).then(function(d) {
        if (!d || !d.ok) return;
        var c = d.campaigns.find(function(x) { return parseInt(x.id) === id; });
        if (c) openCampaignModal(c);
    });
}

function saveCampaign() {
    var id = parseInt(document.getElementById('campId').value) || 0;
    var payload = {
        action: id > 0 ? 'update_campaign' : 'create_campaign',
        id: id,
        name: document.getElementById('campName').value,
        advertiser: document.getElementById('campAdvertiser').value,
        type: document.getElementById('campType').value,
        cpm_rate: parseFloat(document.getElementById('campCpm').value) || 0,
        audio_file_path: document.getElementById('campAudioPath').value,
        duration_sec: parseInt(document.getElementById('campDuration').value) || 30,
        max_impressions: parseInt(document.getElementById('campMaxImpressions').value) || null,
        click_url: document.getElementById('campClickUrl').value,
        start_date: document.getElementById('campStartDate').value || null,
        end_date: document.getElementById('campEndDate').value || null,
        targeting_json: document.getElementById('campTargeting').value || null,
        is_active: document.getElementById('campActive').checked ? 1 : 0,
    };

    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify(payload)}).then(function(d) {
        if (d && d.ok) {
            mc1Toast(d.message || 'Campaign saved', 'ok');
            closeCampaignModal();
            loadCampaigns();
            loadOverview();
        } else {
            mc1Toast((d && d.error) || 'Failed to save campaign', 'err');
        }
    });
}

function deleteCampaign(id, name) {
    if (!confirm('Delete campaign "' + name + '"? This also removes all its placements.')) return;
    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({action:'delete_campaign', id:id})}).then(function(d) {
        if (d && d.ok) {
            mc1Toast('Campaign deleted', 'ok');
            loadCampaigns();
            loadOverview();
        } else {
            mc1Toast((d && d.error) || 'Failed to delete', 'err');
        }
    });
}

function uploadAdAudio(input) {
    if (!input.files || !input.files[0]) return;
    var fd = new FormData();
    fd.append('audio_file', input.files[0]);
    fd.append('action', 'upload_ad_audio');

    /* We use raw fetch for multipart upload */
    fetch('/app/api/ads.php', {
        method: 'POST',
        body: fd,
        credentials: 'include'
    }).then(function(r) { return r.json(); }).then(function(d) {
        if (d && d.ok) {
            document.getElementById('campAudioPath').value = d.file_path;
            if (d.duration_sec > 0) {
                document.getElementById('campDuration').value = d.duration_sec;
            }
            mc1Toast('Ad audio uploaded: ' + d.file_name, 'ok');
        } else {
            mc1Toast((d && d.error) || 'Upload failed', 'err');
        }
    });
    input.value = '';
}

/* ── Sponsors ── */

function loadSponsors() {
    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({action:'list_sponsors'})}).then(function(d) {
        var grid = document.getElementById('sponsorGrid');
        if (!d || !d.ok || !d.sponsors || d.sponsors.length === 0) {
            grid.innerHTML = '<div style="text-align:center;padding:40px;color:var(--muted);grid-column:1/-1"><i class="fa-solid fa-handshake" style="font-size:32px;display:block;margin-bottom:10px;color:var(--border)"></i>No sponsors yet. Add your first sponsor.</div>';
            return;
        }

        var html = '';
        d.sponsors.forEach(function(s) {
            var logo = s.logo_path
                ? '<img src="' + esc(s.logo_path) + '" alt="' + esc(s.name) + '">'
                : '<i class="fa-solid fa-building"></i>';
            html += '<div class="sp-card">';
            html += '<div class="sp-hdr">';
            html += '<div class="sp-logo">' + logo + '</div>';
            html += '<div><div class="sp-name">' + esc(s.name) + '</div>';
            html += '<div class="sp-meta">' + esc(s.contact_email || 'No email') + '</div></div>';
            html += '</div>';
            if (s.website_url) html += '<div style="font-size:12px;color:var(--teal);margin-bottom:6px"><i class="fa-solid fa-globe" style="margin-right:4px"></i><a href="' + esc(s.website_url) + '" target="_blank">' + esc(s.website_url) + '</a></div>';
            if (s.notes) html += '<div style="font-size:12px;color:var(--text-dim);margin-bottom:6px">' + esc(s.notes).substring(0, 100) + '</div>';
            html += '<div style="font-size:11px;color:var(--muted)">Total spent: <strong>' + fmtMoney(s.total_spent) + '</strong></div>';
            html += '<div class="sp-acts">';
            html += '<button class="btn btn-xs btn-secondary" onclick="editSponsor(' + s.id + ')"><i class="fa-solid fa-pen"></i> Edit</button>';
            html += '<button class="btn btn-xs btn-secondary" onclick="deleteSponsor(' + s.id + ',' + esc(JSON.stringify(s.name)) + ')"><i class="fa-solid fa-trash"></i></button>';
            html += '</div></div>';
        });
        grid.innerHTML = html;
    });
}

function openSponsorModal(data) {
    document.getElementById('spId').value = (data && data.id) ? data.id : 0;
    document.getElementById('sponsorModalTitle').textContent = (data && data.id) ? 'Edit Sponsor' : 'New Sponsor';
    document.getElementById('spName').value = (data && data.name) || '';
    document.getElementById('spEmail').value = (data && data.contact_email) || '';
    document.getElementById('spWebsite').value = (data && data.website_url) || '';
    document.getElementById('spLogo').value = (data && data.logo_path) || '';
    document.getElementById('spNotes').value = (data && data.notes) || '';
    document.getElementById('spActive').checked = data ? !!parseInt(data.is_active) : true;
    document.getElementById('sponsorModal').classList.add('open');
}

function closeSponsorModal() {
    document.getElementById('sponsorModal').classList.remove('open');
}

function editSponsor(id) {
    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({action:'list_sponsors'})}).then(function(d) {
        if (!d || !d.ok) return;
        var s = d.sponsors.find(function(x) { return parseInt(x.id) === id; });
        if (s) openSponsorModal(s);
    });
}

function saveSponsor() {
    var id = parseInt(document.getElementById('spId').value) || 0;
    var payload = {
        action: id > 0 ? 'update_sponsor' : 'create_sponsor',
        id: id,
        name: document.getElementById('spName').value,
        contact_email: document.getElementById('spEmail').value,
        website_url: document.getElementById('spWebsite').value,
        logo_path: document.getElementById('spLogo').value,
        notes: document.getElementById('spNotes').value,
        is_active: document.getElementById('spActive').checked ? 1 : 0,
    };

    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify(payload)}).then(function(d) {
        if (d && d.ok) {
            mc1Toast(d.message || 'Sponsor saved', 'ok');
            closeSponsorModal();
            loadSponsors();
        } else {
            mc1Toast((d && d.error) || 'Failed to save sponsor', 'err');
        }
    });
}

function deleteSponsor(id, name) {
    if (!confirm('Delete sponsor "' + name + '"?')) return;
    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({action:'delete_sponsor', id:id})}).then(function(d) {
        if (d && d.ok) {
            mc1Toast('Sponsor deleted', 'ok');
            loadSponsors();
        } else {
            mc1Toast((d && d.error) || 'Failed to delete', 'err');
        }
    });
}

/* ── Revenue Chart ── */

function loadRevenue() {
    var period = document.getElementById('revPeriod').value;
    var days = parseInt(document.getElementById('revDays').value);

    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({
        action:'revenue_summary', period:period, days:days
    })}).then(function(d) {
        if (!d || !d.ok) return;
        var periods = d.periods || [];
        var labels = periods.map(function(p) { return p.period_label; });
        var revData = periods.map(function(p) { return parseFloat(p.revenue || 0); });
        var impData = periods.map(function(p) { return parseInt(p.impressions || 0); });

        if (revenueChart) revenueChart.destroy();

        var ctx = document.getElementById('revenueChart').getContext('2d');
        revenueChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [
                    {
                        label: 'Revenue ($)',
                        data: revData,
                        borderColor: '#14b8a6',
                        backgroundColor: 'rgba(20,184,166,.1)',
                        fill: true,
                        tension: 0.3,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Impressions',
                        data: impData,
                        borderColor: '#0891b2',
                        backgroundColor: 'rgba(8,145,178,.1)',
                        fill: false,
                        tension: 0.3,
                        yAxisID: 'y1'
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: { mode: 'index', intersect: false },
                plugins: { legend: { labels: { color: '#94a3b8' } } },
                scales: {
                    x: { ticks: { color: '#64748b' }, grid: { color: 'rgba(51,65,85,.4)' } },
                    y: { position: 'left', ticks: { color: '#14b8a6', callback: function(v) { return '$' + v; } }, grid: { color: 'rgba(51,65,85,.3)' } },
                    y1: { position: 'right', ticks: { color: '#0891b2' }, grid: { display: false } }
                }
            }
        });
    });
}

/* ── Performance table ── */

function loadPerformance() {
    var days = parseInt(document.getElementById('perfDays').value);

    mc1Api('/app/api/ads.php', {method:'POST', body:JSON.stringify({action:'ad_stats', days:days})}).then(function(d) {
        var body = document.getElementById('perfTableBody');
        if (!d || !d.ok || !d.stats || d.stats.length === 0) {
            body.innerHTML = '<tr><td colspan="7" style="text-align:center;padding:40px;color:var(--muted)">No performance data yet</td></tr>';
            return;
        }

        var maxImp = Math.max.apply(null, d.stats.map(function(s) { return parseInt(s.recent_impressions || 0); })) || 1;
        var html = '';
        d.stats.forEach(function(s) {
            var imp = parseInt(s.recent_impressions || 0);
            var clicks = parseInt(s.recent_clicks || 0);
            var ctr = imp > 0 ? (clicks / imp * 100).toFixed(2) : '0.00';
            var rev = (imp / 1000 * parseFloat(s.cpm_rate || 0)).toFixed(2);
            var fillPct = (imp / maxImp * 100).toFixed(0);

            html += '<tr>';
            html += '<td><strong>' + esc(s.name) + '</strong><div style="font-size:11px;color:var(--muted)">' + esc(s.advertiser || '') + '</div></td>';
            html += '<td><span class="type-badge ' + esc(s.type) + '">' + esc(s.type.replace('_', '-')) + '</span></td>';
            html += '<td>' + fmtNum(imp) + '</td>';
            html += '<td>' + fmtNum(clicks) + '</td>';
            html += '<td>' + ctr + '%</td>';
            html += '<td><strong>' + fmtMoney(rev) + '</strong></td>';
            html += '<td><div class="perf-bar"><div class="perf-bar-inner" style="width:' + fillPct + '%"></div></div></td>';
            html += '</tr>';
        });
        body.innerHTML = html;
    });
}

/* ── Init ── */

document.addEventListener('DOMContentLoaded', function() {
    /* Tab switching */
    document.querySelectorAll('.mon-tab').forEach(function(tab) {
        tab.addEventListener('click', function() {
            switchTab(this.getAttribute('data-tab'));
        });
    });

    /* Close modals on overlay click */
    document.querySelectorAll('.mon-modal').forEach(function(modal) {
        modal.addEventListener('click', function(e) {
            if (e.target === modal) modal.classList.remove('open');
        });
    });

    /* Load data */
    loadOverview();
    loadCampaigns();
    loadSponsors();
});
</script>

<?php require __DIR__ . '/app/inc/footer.php'; ?>
