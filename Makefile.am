# Makefile.am — Root (Mcaster1 DSP Encoder Linux)
#
# Autotools entry point.  All Linux build targets live under src/linux/.
# Windows targets are built by Visual Studio (.vcxproj) — not touched here.

SUBDIRS = src/linux

# Install the web UI files
webuidir = $(WEBROOT)
nobase_webuiDATA = \
    src/linux/web_ui/index.html \
    src/linux/web_ui/login.html \
    src/linux/web_ui/style.css \
    src/linux/web_ui/app.js

EXTRA_DIST = \
    autogen.sh \
    src/linux/make_http_test.sh \
    src/linux/web_ui/index.html \
    src/linux/web_ui/login.html \
    src/linux/web_ui/style.css \
    src/linux/web_ui/app.js \
    README.md
