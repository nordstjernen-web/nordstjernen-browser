/* Nordstjernen — Qt frontend application entry point.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "config.h"

#include "procwindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Nordstjernen"));
    QApplication::setApplicationDisplayName(
        QStringLiteral("Nordstjernen (Qt)"));

    ns_config_init();
    const ns_config *cfg = ns_config_get();
    const QString home = (cfg && cfg->home_url && *cfg->home_url)
        ? QString::fromUtf8(cfg->home_url)
        : QStringLiteral("about:start");

    const QStringList args = QApplication::arguments();
    const QString url = args.size() > 1 ? args.at(1) : home;

    int status;
    {
        ProcWindow window;
        window.show();
        window.addTab(url);
        status = app.exec();
    }
    ns_config_shutdown();
    return status;
}
