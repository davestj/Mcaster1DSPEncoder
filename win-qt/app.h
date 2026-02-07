/*
 * Mcaster1DSPEncoder — macOS Qt 6 Build
 * app.h — Application class
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef MC1_APP_H
#define MC1_APP_H

#include <QApplication>
#include <QString>

namespace mc1 {

class AudioPipeline;
class MainWindow;

class App : public QApplication {
    Q_OBJECT

public:
    App(int &argc, char **argv);
    ~App() override;

    static App *instance() { return static_cast<App *>(qApp); }

    /* Theme management */
    enum class Theme { Native, Branded };
    void      setTheme(Theme t);
    Theme     theme() const { return theme_; }

    /* Version info */
    static QString versionString() { return QStringLiteral("1.0.0"); }
    static QString buildPhase()    { return QStringLiteral("Phase M10"); }

    MainWindow *mainWindow() const { return main_window_; }

Q_SIGNALS:
    void themeChanged();

private:
    void applyNativeTheme();
    void applyBrandedTheme();

    MainWindow    *main_window_ = nullptr;
    AudioPipeline *pipeline_    = nullptr;
    Theme          theme_       = Theme::Native;
};

} // namespace mc1

#endif // MC1_APP_H
