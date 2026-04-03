/**
 * mobile-nav.js -- Mobile navigation logic for Mcaster1 DSP Encoder UI
 *
 * File:    src/linux/web_ui/js/mobile-nav.js
 * Author:  Dave St. John <davestj@gmail.com>
 * Date:    2026-03-27
 * Phase:   v1.9.0 -- Mobile-Responsive Web UI
 * Purpose: Hamburger menu toggle, bottom tab bar active state, "More" menu
 *          slide-up panel, swipe-from-left to open sidebar (raw touch events,
 *          no library dependency).
 *
 * Standards:
 *   - Raw touch events only (no Hammer.js)
 *   - Works with responsive.css media queries
 *   - Does not modify desktop behavior
 */

(function() {
'use strict';

var SWIPE_THRESHOLD = 50;  // minimum px horizontal swipe to trigger
var SWIPE_MAX_Y     = 80;  // max vertical movement allowed during swipe
var EDGE_ZONE       = 30;  // left edge zone in px where swipe starts

/* ── DOM references (resolved after DOMContentLoaded) ── */
var sidebar, backdrop, morePanel, moreBackdrop, tabBar;

/* ── Detect mobile breakpoint ── */
function isMobile() {
    return window.innerWidth <= 768;
}

/* ── Sidebar toggle ── */
function openSidebar() {
    if (!sidebar) return;
    sidebar.classList.add('open');
    if (backdrop) backdrop.classList.add('active');
}

function closeSidebar() {
    if (!sidebar) return;
    sidebar.classList.remove('open');
    if (backdrop) backdrop.classList.remove('active');
}

function toggleSidebar() {
    if (sidebar && sidebar.classList.contains('open')) {
        closeSidebar();
    } else {
        openSidebar();
    }
}

/* ── "More" menu toggle ── */
function openMore() {
    if (morePanel) morePanel.classList.add('open');
    if (moreBackdrop) moreBackdrop.classList.add('active');
}

function closeMore() {
    if (morePanel) morePanel.classList.remove('open');
    if (moreBackdrop) moreBackdrop.classList.remove('active');
}

function toggleMore() {
    if (morePanel && morePanel.classList.contains('open')) {
        closeMore();
    } else {
        closeMore(); // close first in case it was open
        openMore();
    }
}

/* ── Swipe-from-left-edge to open sidebar ── */
var touchStartX = 0;
var touchStartY = 0;
var touchTracking = false;

function onTouchStart(e) {
    if (!isMobile()) return;
    var touch = e.touches[0];
    if (touch.clientX < EDGE_ZONE) {
        touchStartX = touch.clientX;
        touchStartY = touch.clientY;
        touchTracking = true;
    }
}

function onTouchMove(e) {
    if (!touchTracking) return;
    // Do nothing during move -- we evaluate on end
}

function onTouchEnd(e) {
    if (!touchTracking) return;
    touchTracking = false;

    var touch = e.changedTouches[0];
    var dx = touch.clientX - touchStartX;
    var dy = Math.abs(touch.clientY - touchStartY);

    if (dx > SWIPE_THRESHOLD && dy < SWIPE_MAX_Y) {
        openSidebar();
    }
}

/* ── Set active tab in bottom bar ── */
function setActiveTab() {
    if (!tabBar) return;
    var path = window.location.pathname;
    var links = tabBar.querySelectorAll('a[data-page]');
    for (var i = 0; i < links.length; i++) {
        var pages = links[i].getAttribute('data-page').split(',');
        var isActive = false;
        for (var j = 0; j < pages.length; j++) {
            if (path.indexOf(pages[j]) !== -1) {
                isActive = true;
                break;
            }
        }
        if (isActive) {
            links[i].classList.add('active');
        } else {
            links[i].classList.remove('active');
        }
    }
}

/* ── Orientation change handler ── */
function onOrientationChange() {
    // Close overlays on orientation change to prevent stale UI
    closeSidebar();
    closeMore();
}

/* ── Init ── */
document.addEventListener('DOMContentLoaded', function() {
    sidebar      = document.querySelector('.sidebar');
    backdrop     = document.querySelector('.sidebar-backdrop');
    morePanel    = document.querySelector('.mobile-more-panel');
    moreBackdrop = document.querySelector('.mobile-more-backdrop');
    tabBar       = document.querySelector('.mobile-tab-bar');

    // Create sidebar backdrop if not present
    if (!backdrop && sidebar) {
        backdrop = document.createElement('div');
        backdrop.className = 'sidebar-backdrop';
        sidebar.parentNode.insertBefore(backdrop, sidebar);
        backdrop.addEventListener('click', closeSidebar);
    }

    // Create "more" backdrop if not present
    if (!moreBackdrop && morePanel) {
        moreBackdrop = document.createElement('div');
        moreBackdrop.className = 'mobile-more-backdrop';
        morePanel.parentNode.insertBefore(moreBackdrop, morePanel);
        moreBackdrop.addEventListener('click', closeMore);
    }

    // Backdrop click closes sidebar
    if (backdrop) {
        backdrop.addEventListener('click', closeSidebar);
    }

    // Wire hamburger button -- override the inline onclick from header.php
    var hamburger = document.querySelector('.hamburger');
    if (hamburger) {
        hamburger.removeAttribute('onclick');
        hamburger.addEventListener('click', function(e) {
            e.stopPropagation();
            closeMore();
            toggleSidebar();
        });
    }

    // Wire "More" button in tab bar
    var moreBtn = document.querySelector('.mobile-tab-more');
    if (moreBtn) {
        moreBtn.addEventListener('click', function(e) {
            e.preventDefault();
            e.stopPropagation();
            closeSidebar();
            toggleMore();
        });
    }

    // Swipe-from-left
    document.addEventListener('touchstart', onTouchStart, { passive: true });
    document.addEventListener('touchmove', onTouchMove, { passive: true });
    document.addEventListener('touchend', onTouchEnd, { passive: true });

    // Set active tab
    setActiveTab();

    // Orientation change
    window.addEventListener('orientationchange', onOrientationChange);
    window.addEventListener('resize', function() {
        if (!isMobile()) {
            closeSidebar();
            closeMore();
        }
    });

    // Close sidebar on nav item click (mobile)
    if (sidebar) {
        sidebar.querySelectorAll('.nav-item').forEach(function(item) {
            item.addEventListener('click', function() {
                if (isMobile()) closeSidebar();
            });
        });
    }

    // Close "More" panel on link click
    if (morePanel) {
        morePanel.querySelectorAll('a').forEach(function(link) {
            link.addEventListener('click', closeMore);
        });
    }
});

// Export for use by other scripts if needed
window.mc1MobileNav = {
    openSidebar: openSidebar,
    closeSidebar: closeSidebar,
    toggleSidebar: toggleSidebar,
    openMore: openMore,
    closeMore: closeMore,
    toggleMore: toggleMore
};

})();
