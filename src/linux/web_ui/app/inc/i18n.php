<?php
/*
 * i18n.php -- Mcaster1 DSP Encoder Internationalization Framework
 *
 * Provides lightweight string translation for the web UI.
 * Language files live in web_ui/lang/{code}.php and return
 * a flat PHP array of key => translated_string.
 *
 * Usage in PHP templates:
 *   <?= __('Dashboard') ?>
 *   <?= __('Welcome back, {name}', ['name' => $user]) ?>
 *   <?= __n('%d listener', '%d listeners', $count) ?>
 *
 * Usage in JS (injected via footer/header):
 *   mc1i18n.t('Start')
 *   mc1i18n.t('Stop')
 *
 * Language detection order:
 *   1. $_COOKIE['mc1_lang']
 *   2. Accept-Language header
 *   3. Default: 'en'
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

if (!defined('MC1_BOOT')) {
    http_response_code(403);
    echo '403 Forbidden';
    return;
}

/* -- Supported languages --------------------------------------------- */

$mc1_i18n_supported = [
    'en' => 'English',
    'es' => 'Espa&ntilde;ol',
    'fr' => 'Fran&ccedil;ais',
    'de' => 'Deutsch',
    'pt' => 'Portugu&ecirc;s',
    'ja' => '&#26085;&#26412;&#35486;',
];

/* -- Detect current language ----------------------------------------- */

$mc1_i18n_lang = 'en';

// 1. Cookie
if (!empty($_COOKIE['mc1_lang']) && isset($mc1_i18n_supported[$_COOKIE['mc1_lang']])) {
    $mc1_i18n_lang = $_COOKIE['mc1_lang'];
}
// 2. Accept-Language header
elseif (!empty($_SERVER['HTTP_ACCEPT_LANGUAGE'])) {
    $accept = $_SERVER['HTTP_ACCEPT_LANGUAGE'];
    // Parse "en-US,en;q=0.9,es;q=0.8" -> try each code
    $parts = explode(',', $accept);
    foreach ($parts as $part) {
        $code = strtolower(trim(explode(';', $part)[0]));
        // Try exact match first
        if (isset($mc1_i18n_supported[$code])) {
            $mc1_i18n_lang = $code;
            break;
        }
        // Try base language (e.g., "en-US" -> "en")
        $base = explode('-', $code)[0];
        if (isset($mc1_i18n_supported[$base])) {
            $mc1_i18n_lang = $base;
            break;
        }
    }
}

/* -- Load language strings ------------------------------------------- */

$mc1_i18n_strings = [];

// Always load English as the base (fallback for missing keys)
$en_file = __DIR__ . '/../../lang/en.php';
if (is_file($en_file)) {
    $mc1_i18n_strings = (array)(include $en_file);
}

// Overlay target language if not English
if ($mc1_i18n_lang !== 'en') {
    $lang_file = __DIR__ . '/../../lang/' . $mc1_i18n_lang . '.php';
    if (is_file($lang_file)) {
        $target_strings = (array)(include $lang_file);
        // Merge: target overrides English, missing keys fall back to English
        $mc1_i18n_strings = array_merge($mc1_i18n_strings, $target_strings);
    }
}

/* -- Translation functions ------------------------------------------- */

/**
 * Translate a string key, with optional placeholder replacements.
 *
 * @param string $key          Translation key (or the English text itself)
 * @param array  $replacements Associative array of {placeholder} => value
 * @return string
 */
function __($key, $replacements = []) {
    global $mc1_i18n_strings;
    $str = isset($mc1_i18n_strings[$key]) ? $mc1_i18n_strings[$key] : $key;
    foreach ($replacements as $k => $v) {
        $str = str_replace('{' . $k . '}', $v, $str);
    }
    return $str;
}

/**
 * Translate with pluralization.
 *
 * @param string $singular Singular form (with %d placeholder)
 * @param string $plural   Plural form (with %d placeholder)
 * @param int    $count    The count to determine singular/plural
 * @return string
 */
function __n($singular, $plural, $count) {
    global $mc1_i18n_strings;
    $key = ($count == 1) ? $singular : $plural;
    $str = isset($mc1_i18n_strings[$key]) ? $mc1_i18n_strings[$key] : $key;
    return sprintf($str, $count);
}

/**
 * Get current language code.
 * @return string
 */
function mc1_i18n_lang() {
    global $mc1_i18n_lang;
    return $mc1_i18n_lang;
}

/**
 * Get all supported languages.
 * @return array  code => display name
 */
function mc1_i18n_languages() {
    global $mc1_i18n_supported;
    return $mc1_i18n_supported;
}

/**
 * Get the full translation strings array (for JS injection).
 * @return array
 */
function mc1_i18n_strings() {
    global $mc1_i18n_strings;
    return $mc1_i18n_strings;
}
