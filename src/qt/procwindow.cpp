/* Nordstjernen — Qt tabbed browser window with one renderer process per tab.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "procwindow.h"
#include "procview.h"
#include "rproc_http.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QKeyEvent>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "version.h"

ProcWindow::ProcWindow(QWidget *parent) : QMainWindow(parent) {
    const QPixmap logo(QStringLiteral(":/nordstjernen.gif"));

    setWindowTitle(QStringLiteral("Nordstjernen (Qt)"));
    setWindowIcon(QIcon(logo));
    resize(1024, 768);

    QToolBar *toolbar = addToolBar(QStringLiteral("Navigation"));
    toolbar->setMovable(false);

    m_backAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_ArrowBack), QStringLiteral("Back"));
    m_forwardAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_ArrowForward),
        QStringLiteral("Forward"));
    m_reloadAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_BrowserReload),
        QStringLiteral("Reload"));
    m_homeAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_DirHomeIcon),
        QStringLiteral("Home"));

    m_backAction->setShortcuts(QKeySequence::Back);
    m_forwardAction->setShortcuts(QKeySequence::Forward);
    m_reloadAction->setShortcuts(QKeySequence::Refresh);
    m_homeAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Home));
    m_backAction->setToolTip(QStringLiteral("Back (Alt+Left)"));
    m_forwardAction->setToolTip(QStringLiteral("Forward (Alt+Right)"));
    m_reloadAction->setToolTip(QStringLiteral("Reload (F5)"));
    m_homeAction->setToolTip(QStringLiteral("Home (Alt+Home)"));

    m_address = new QLineEdit(this);
    m_address->setClearButtonEnabled(true);
    m_address->setPlaceholderText(
        QStringLiteral("Enter a URL and press Enter"));
    toolbar->addWidget(m_address);

    QMenu *appMenu = new QMenu(this);
    QAction *menuNewTab = appMenu->addAction(QStringLiteral("New Tab"));
    connect(menuNewTab, &QAction::triggered, this, &ProcWindow::onNewTab);
    QAction *menuFind = appMenu->addAction(QStringLiteral("Find…"));
    menuFind->setShortcut(QKeySequence::Find);
    connect(menuFind, &QAction::triggered, this, [this]() {
        if (ProcView *view = currentView()) {
            view->setFocus();
            QKeyEvent ev(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier);
            QApplication::sendEvent(view, &ev);
        }
    });
    appMenu->addSeparator();
    QAction *menuPdf = appMenu->addAction(QStringLiteral("Save Page as PDF…"));
    connect(menuPdf, &QAction::triggered, this, [this]() {
        if (ProcView *view = currentView())
            view->savePageAs(true);
    });
    QAction *menuPng =
        appMenu->addAction(QStringLiteral("Save Page as Image…"));
    connect(menuPng, &QAction::triggered, this, [this]() {
        if (ProcView *view = currentView())
            view->savePageAs(false);
    });
    QAction *menuConsole =
        appMenu->addAction(QStringLiteral("JavaScript Console"));
    connect(menuConsole, &QAction::triggered, this, [this]() {
        if (ProcView *view = currentView())
            view->toggleConsole();
    });
    QAction *menuTaskMgr = appMenu->addAction(QStringLiteral("Task Manager"));
    menuTaskMgr->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Escape));
    connect(menuTaskMgr, &QAction::triggered, this, &ProcWindow::onTaskManager);
    appMenu->addSeparator();
    QAction *menuAbout =
        appMenu->addAction(QStringLiteral("About Nordstjernen"));
    connect(menuAbout, &QAction::triggered, this, &ProcWindow::onAbout);

    QToolButton *menuButton = new QToolButton(this);
    menuButton->setText(QStringLiteral("☰"));
    menuButton->setToolTip(QStringLiteral("Menu"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    menuButton->setMenu(appMenu);
    menuButton->setAutoRaise(true);
    toolbar->addWidget(menuButton);

    QToolButton *brand = new QToolButton(this);
    if (!logo.isNull())
        brand->setIcon(QIcon(logo.scaledToHeight(24, Qt::SmoothTransformation)));
    brand->setToolTip(QStringLiteral("Visit nordstjernen.org"));
    brand->setAutoRaise(true);
    connect(brand, &QToolButton::clicked, this, [this]() {
        if (ProcView *view = currentView())
            view->load(QStringLiteral("https://nordstjernen.org"));
        else
            addTab(QStringLiteral("https://nordstjernen.org"));
    });
    toolbar->addWidget(brand);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);

    statusBar()->showMessage(QStringLiteral("Ready"));

    connect(m_address, &QLineEdit::returnPressed, this,
            &ProcWindow::onAddressEntered);
    connect(m_backAction, &QAction::triggered, this, &ProcWindow::onBack);
    connect(m_forwardAction, &QAction::triggered, this,
            &ProcWindow::onForward);
    connect(m_reloadAction, &QAction::triggered, this, &ProcWindow::onReload);
    connect(m_homeAction, &QAction::triggered, this, &ProcWindow::onHome);
    connect(m_tabs, &QTabWidget::currentChanged, this,
            &ProcWindow::onCurrentChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this,
            &ProcWindow::onCloseTab);

    QAction *newTab = new QAction(this);
    newTab->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_T),
                          QKeySequence::AddTab});
    connect(newTab, &QAction::triggered, this, &ProcWindow::onNewTab);
    addAction(newTab);

    QAction *closeTab = new QAction(this);
    closeTab->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_W),
                            QKeySequence::Close});
    connect(closeTab, &QAction::triggered, this,
            [this]() { onCloseTab(m_tabs->currentIndex()); });
    addAction(closeTab);

    QAction *focusAddress = new QAction(this);
    focusAddress->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_L),
                                QKeySequence(Qt::ALT | Qt::Key_D)});
    connect(focusAddress, &QAction::triggered, this, [this]() {
        m_address->setFocus();
        m_address->selectAll();
    });
    addAction(focusAddress);

    QAction *zoomIn = new QAction(this);
    zoomIn->setShortcuts({QKeySequence::ZoomIn,
                          QKeySequence(Qt::CTRL | Qt::Key_Equal),
                          QKeySequence(Qt::CTRL | Qt::Key_Plus)});
    connect(zoomIn, &QAction::triggered, this, [this]() {
        if (ProcView *view = currentView())
            view->zoomIn();
    });
    addAction(zoomIn);

    QAction *zoomOut = new QAction(this);
    zoomOut->setShortcuts({QKeySequence::ZoomOut,
                           QKeySequence(Qt::CTRL | Qt::Key_Minus)});
    connect(zoomOut, &QAction::triggered, this, [this]() {
        if (ProcView *view = currentView())
            view->zoomOut();
    });
    addAction(zoomOut);

    QAction *zoomReset = new QAction(this);
    zoomReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(zoomReset, &QAction::triggered, this, [this]() {
        if (ProcView *view = currentView())
            view->zoomReset();
    });
    addAction(zoomReset);

    updateNavActions();
}

ProcView *ProcWindow::viewAt(int index) const {
    return qobject_cast<ProcView *>(m_tabs->widget(index));
}

ProcView *ProcWindow::currentView() const {
    return viewAt(m_tabs->currentIndex());
}

ProcView *ProcWindow::addTab(const QString &url, bool foreground) {
    ProcView *view = new ProcView(m_tabs);
    connectView(view);
    const int index = m_tabs->addTab(view, QStringLiteral("New Tab"));
    if (foreground)
        m_tabs->setCurrentIndex(index);
    view->load(normalizeUrl(url));
    return view;
}

void ProcWindow::connectView(ProcView *view) {
    connect(view, &ProcView::titleChanged, this, [this, view](const QString &t) {
        const int index = m_tabs->indexOf(view);
        if (index < 0)
            return;
        const QString label = t.isEmpty() ? QStringLiteral("Untitled") : t;
        m_tabs->setTabText(index, label.left(40));
        m_tabs->setTabToolTip(index, label);
        if (view == currentView())
            setWindowTitle(label + QStringLiteral(" — Nordstjernen (Qt)"));
    });
    connect(view, &ProcView::urlChanged, this, [this, view](const QString &u) {
        if (view == currentView())
            m_address->setText(u);
    });
    connect(view, &ProcView::statusMessage, this, [this, view](const QString &m) {
        if (view == currentView())
            statusBar()->showMessage(m);
    });
    connect(view, &ProcView::loadingChanged, this, [this, view](bool loading) {
        if (view == currentView())
            m_reloadAction->setEnabled(!loading);
    });
    connect(view, &ProcView::historyChanged, this, [this, view]() {
        if (view == currentView())
            updateNavActions();
    });
    connect(view, &ProcView::linkRequestedInNewTab, this,
            [this](const QString &url) { addTab(url, false); });
}

void ProcWindow::updateChrome() {
    ProcView *view = currentView();
    if (!view) {
        m_address->clear();
        setWindowTitle(QStringLiteral("Nordstjernen (Qt)"));
        updateNavActions();
        return;
    }
    m_address->setText(view->currentUrl());
    const QString title = view->currentTitle();
    setWindowTitle((title.isEmpty() ? QStringLiteral("Nordstjernen")
                                    : title) +
                   QStringLiteral(" — Nordstjernen (Qt)"));
    updateNavActions();
}

void ProcWindow::updateNavActions() {
    ProcView *view = currentView();
    m_backAction->setEnabled(view && view->canGoBack());
    m_forwardAction->setEnabled(view && view->canGoForward());
    m_reloadAction->setEnabled(view != nullptr);
}

QString ProcWindow::normalizeUrl(const QString &input) const {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return trimmed;

    if (trimmed.startsWith(QStringLiteral("about:")) ||
        trimmed.startsWith(QStringLiteral("file:")))
        return trimmed;

    const QUrl url = QUrl::fromUserInput(trimmed);
    if (url.isValid() && !url.scheme().isEmpty())
        return url.toString();

    return trimmed;
}

void ProcWindow::onAddressEntered() {
    const QString resolved = normalizeUrl(m_address->text());
    if (resolved.isEmpty())
        return;
    ProcView *view = currentView();
    if (!view) {
        addTab(resolved);
        return;
    }
    m_address->setText(resolved);
    view->load(resolved);
}

void ProcWindow::onBack() {
    if (ProcView *view = currentView())
        view->back();
}

void ProcWindow::onForward() {
    if (ProcView *view = currentView())
        view->forward();
}

void ProcWindow::onReload() {
    if (ProcView *view = currentView())
        view->reload();
}

void ProcWindow::onHome() {
    ProcView *view = currentView();
    if (!view) {
        addTab(QStringLiteral("about:start"));
        return;
    }
    view->load(QStringLiteral("about:start"));
}

void ProcWindow::onNewTab() {
    addTab(QStringLiteral("about:start"));
    m_address->setFocus();
    m_address->selectAll();
}

void ProcWindow::onCloseTab(int index) {
    QWidget *widget = m_tabs->widget(index);
    if (!widget)
        return;
    m_tabs->removeTab(index);
    widget->deleteLater();
    if (m_tabs->count() == 0)
        close();
}

void ProcWindow::onCurrentChanged(int index) {
    (void)index;
    updateChrome();
}

void ProcWindow::onAbout() {
    QString body = QStringLiteral(
        "<h3>Nordstjernen %1 (Qt)</h3>"
        "<p>Northstar — the legendary web browser.</p>"
        "<p>A clean-room browser in C with libcurl, rendering each tab in "
        "a sandboxed process. This is the Qt 6 shell.</p>")
        .arg(QStringLiteral(NS_VERSION));
#ifdef NS_HAVE_AI
    body += QStringLiteral(
        "<p><b>Built with Llama.</b> The optional local AI assistant can run "
        "Meta Llama 3.1 (Llama 3.1 Community License) and Alibaba Qwen2.5 "
        "models that you choose to download.</p>");
#endif
    body += QStringLiteral(
        "<p>Includes third-party open-source software; see "
        "THIRD-PARTY-LICENSES.md for full notices.</p>"
        "<p><a href=\"https://nordstjernen.org\">nordstjernen.org</a></p>"
        "<p>Nordstjernen Source License v1.0 — © 2026 Andreas Røsdal</p>");
    QMessageBox::about(this, QStringLiteral("About Nordstjernen"), body);
}

void ProcWindow::refreshTaskManager() {
    if (!m_taskTable)
        return;
    const int n = m_tabs ? m_tabs->count() : 0;
    m_taskTable->setRowCount(n);
    for (int i = 0; i < n; ++i) {
        ProcView *view = viewAt(i);
        const int pid = view ? view->rendererPid() : -1;
        char state[32] = "starting";
        long rss = -1;
        if (pid > 0)
            ns_rproc_http_proc_info(pid, state, sizeof state, &rss);

        QString title = view ? view->currentTitle() : QString();
        if (title.isEmpty())
            title = view ? view->currentUrl() : QString();
        if (title.isEmpty())
            title = QStringLiteral("New Tab");

        m_taskTable->setItem(i, 0, new QTableWidgetItem(title));
        m_taskTable->setItem(i, 1, new QTableWidgetItem(
            pid > 0 ? QString::number(pid) : QStringLiteral("—")));
        m_taskTable->setItem(i, 2,
            new QTableWidgetItem(QString::fromUtf8(state)));
        m_taskTable->setItem(i, 3, new QTableWidgetItem(
            rss >= 0 ? QString::asprintf("%.1f MB", rss / 1024.0)
                     : QStringLiteral("—")));
    }
}

void ProcWindow::onTaskManager() {
    if (m_taskMgr) {
        m_taskMgr->raise();
        m_taskMgr->activateWindow();
        return;
    }

    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("Task Manager — Nordstjernen"));
    dlg->resize(560, 380);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *vbox = new QVBoxLayout(dlg);
    QTableWidget *table = new QTableWidget(0, 4, dlg);
    table->setHorizontalHeaderLabels(
        { QStringLiteral("Task"), QStringLiteral("Process ID"),
          QStringLiteral("State"), QStringLiteral("Memory") });
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    vbox->addWidget(table);

    QHBoxLayout *bar = new QHBoxLayout();
    bar->addStretch(1);
    QPushButton *refreshBtn = new QPushButton(QStringLiteral("Refresh"), dlg);
    QPushButton *endBtn = new QPushButton(QStringLiteral("End task"), dlg);
    bar->addWidget(refreshBtn);
    bar->addWidget(endBtn);
    vbox->addLayout(bar);

    m_taskMgr = dlg;
    m_taskTable = table;

    connect(refreshBtn, &QPushButton::clicked, this,
            &ProcWindow::refreshTaskManager);
    connect(endBtn, &QPushButton::clicked, this, [this]() {
        if (!m_taskTable)
            return;
        const int row = m_taskTable->currentRow();
        ProcView *view = row >= 0 ? viewAt(row) : nullptr;
        if (view)
            view->endTask();
        refreshTaskManager();
    });
    connect(dlg, &QObject::destroyed, this, [this]() {
        m_taskMgr = nullptr;
        m_taskTable = nullptr;
    });

    QTimer *timer = new QTimer(dlg);
    timer->setInterval(1500);
    connect(timer, &QTimer::timeout, this, &ProcWindow::refreshTaskManager);
    timer->start();

    refreshTaskManager();
    dlg->show();
}
