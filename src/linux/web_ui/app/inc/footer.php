<?php
if (!defined('MC1_BOOT')) {
    http_response_code(403);
    exit('403 Forbidden');
}
?>
  </main><!-- /main-content -->
</div><!-- /dash-body -->

<!-- Toast notification -->
<div id="toast" class="hidden">
  <i id="toast-icon" class="fa-solid fa-circle-check"></i>
  <span id="toast-msg"></span>
</div>

<script>
// Minimal shared JS for PHP pages
(function () {
  function showToast(msg, type) {
    var t = document.getElementById('toast');
    var i = document.getElementById('toast-icon');
    var s = document.getElementById('toast-msg');
    if (!t) return;
    t.className = type === 'error' ? 'toast-error' : '';
    i.className = type === 'error'
      ? 'fa-solid fa-circle-xmark'
      : 'fa-solid fa-circle-check';
    s.textContent = msg;
    t.style.display = 'flex';
    setTimeout(function () { t.style.display = 'none'; }, 3500);
  }
  window.mc1Toast = showToast;

  // Confirm delete buttons
  document.querySelectorAll('[data-confirm]').forEach(function (el) {
    el.addEventListener('click', function (e) {
      if (!confirm(el.dataset.confirm)) e.preventDefault();
    });
  });
})();
</script>
</body>
</html>
