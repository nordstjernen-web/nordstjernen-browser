/* Nordstjernen — Qt frontend application entry point.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "config.h"
#include "net.h"

#include "proc_limits.h"
#include "procwindow.h"
#include "rproc_inproc.h"

#include <glib.h>

#include <QApplication>
#include <QTimer>

static bool singleProcessEnv() {
    const QByteArray env = qgetenv(NS_PROC_SINGLE_PROCESS_ENV);
    return !env.isEmpty() && env != "0";
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Nordstjernen"));
    QApplication::setApplicationDisplayName(
        QStringLiteral("Nordstjernen (Qt)"));

    ns_config_init();
    ns_net_init();
    const ns_config *cfg = ns_config_get();
    const QString home = (cfg && cfg->home_url && *cfg->home_url)
        ? QString::fromUtf8(cfg->home_url)
        : QStringLiteral("about:start");

    const QStringList args = QApplication::arguments();
    QString url = home;
    bool urlSet = false;
    bool singleProcess = singleProcessEnv();
    for (int i = 1; i < args.size(); i++) {
        const QString &arg = args.at(i);
        if (arg == QStringLiteral("--single-process")) {
            singleProcess = true;
        } else if (!arg.startsWith(QLatin1Char('-')) && !urlSet) {
            url = arg;
            urlSet = true;
        }
    }
    if (singleProcess)
        ns_rproc_single_process_enable();

    int status;
    {
        QTimer glibPump;
        if (ns_rproc_single_process_enabled()) {
            QObject::connect(&glibPump, &QTimer::timeout, []() {
                while (g_main_context_iteration(nullptr, FALSE)) { }
            });
            glibPump.start(8);
        }
        ProcWindow window;
        window.show();
        window.addTab(url);
        status = app.exec();
    }
    ns_net_shutdown();
    ns_config_shutdown();
    return status;
}
