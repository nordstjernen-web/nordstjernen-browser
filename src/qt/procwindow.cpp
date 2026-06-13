/* Nordstjernen — Qt tabbed browser window with one renderer process per tab.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "bookmarks.h"
#include "config.h"
#include "net.h"
#include "rproc_http.h"
#include "rproc_inproc.h"

#include "procwindow.h"
#include "procview.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QKeyEvent>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QDesktopServices>
#include <QUrl>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QPointer>
#include <QApplication>
#include <thread>

#include "version.h"

namespace {
const struct { const char *name; const char *url; } kSearchEngines[] = {
    { "DuckDuckGo Lite", "https://lite.duckduckgo.com/lite/?q=%s" },
    { "DuckDuckGo",      "https://duckduckgo.com/?q=%s" },
    { "Baidu",           "https://www.baidu.com/s?wd=%s" },
    { "Google",          "https://www.google.com/search?q=%s" },
    { "Bing",            "https://www.bing.com/search?q=%s" },
    { "Yandex",          "https://yandex.com/search/?text=%s" },
    { "Yahoo",           "https://search.yahoo.com/search?p=%s" },
    { "Yahoo! Japan",    "https://search.yahoo.co.jp/search?p=%s" },
    { "Sogou",           "https://www.sogou.com/web?query=%s" },
    { "Naver",           "https://search.naver.com/search.naver?query=%s" },
    { "Startpage",       "https://www.startpage.com/sp/search?query=%s" },
    { "Brave Search",    "https://search.brave.com/search?q=%s" },
    { "Ecosia",          "https://www.ecosia.org/search?q=%s" },
};
constexpr int kSearchEngineCount =
    int(sizeof(kSearchEngines) / sizeof(kSearchEngines[0]));

QString configuredHomeUrl() {
    const ns_config *cfg = ns_config_get();
    return (cfg && cfg->home_url && *cfg->home_url)
        ? QString::fromUtf8(cfg->home_url)
        : QStringLiteral("about:start");
}
}

ProcWindow::ProcWindow(QWidget *parent) : QMainWindow(parent) {
    const QPixmap logo(QStringLiteral(":/nordstjernen.gif"));

    m_homeUrl = configuredHomeUrl();
    m_bookmarks = ns_bookmarks_load();

    setWindowTitle(QStringLiteral("Nordstjernen"));
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

    m_spinner = new QProgressBar(this);
    m_spinner->setRange(0, 0);
    m_spinner->setTextVisible(false);
    m_spinner->setFixedWidth(60);
    m_spinner->setToolTip(QStringLiteral("Loading"));
    m_spinner->setVisible(false);
    toolbar->addWidget(m_spinner);

    m_address = new QLineEdit(this);
    m_address->setClearButtonEnabled(true);
    m_address->setPlaceholderText(
        QStringLiteral("Enter a URL and press Enter"));
    m_address->installEventFilter(this);
    toolbar->addWidget(m_address);

    QAction *goAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("Go"));
    connect(goAction, &QAction::triggered, this,
            &ProcWindow::onAddressEntered);

    QAction *newTabAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_FileDialogNewFolder),
        QStringLiteral("New tab"));
    connect(newTabAction, &QAction::triggered, this, &ProcWindow::onNewTab);

    QToolButton *bookmarksButton = new QToolButton(this);
    bookmarksButton->setIcon(
        style()->standardIcon(QStyle::SP_DialogYesButton));
    bookmarksButton->setText(QStringLiteral("Bookmarks"));
    bookmarksButton->setToolTip(QStringLiteral("Bookmarks"));
    bookmarksButton->setPopupMode(QToolButton::InstantPopup);
    bookmarksButton->setAutoRaise(true);
    m_bookmarksMenu = new QMenu(bookmarksButton);
    bookmarksButton->setMenu(m_bookmarksMenu);
    connect(m_bookmarksMenu, &QMenu::aboutToShow, this,
            &ProcWindow::rebuildBookmarksMenu);
    toolbar->addWidget(bookmarksButton);

    QMenu *appMenu = new QMenu(this);
    QAction *menuNewTab = appMenu->addAction(QStringLiteral("New Tab"));
    connect(menuNewTab, &QAction::triggered, this, &ProcWindow::onNewTab);
    QAction *menuReload = appMenu->addAction(QStringLiteral("Reload"));
    connect(menuReload, &QAction::triggered, this, &ProcWindow::onReload);
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
    QAction *menuDownloads = appMenu->addAction(QStringLiteral("Downloads"));
    menuDownloads->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    connect(menuDownloads, &QAction::triggered, this, &ProcWindow::showDownloads);
    QAction *menuTaskMgr = appMenu->addAction(QStringLiteral("Task Manager"));
    menuTaskMgr->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Escape));
    connect(menuTaskMgr, &QAction::triggered, this, &ProcWindow::onTaskManager);
    QAction *menuSettings = appMenu->addAction(QStringLiteral("Settings"));
    menuSettings->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(menuSettings, &QAction::triggered, this, &ProcWindow::onSettings);
    appMenu->addSeparator();
    QAction *menuAbout =
        appMenu->addAction(QStringLiteral("About Nordstjernen"));
    connect(menuAbout, &QAction::triggered, this, &ProcWindow::onAbout);

    addAction(menuFind);
    addAction(menuDownloads);
    addAction(menuTaskMgr);
    addAction(menuSettings);

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

    QAction *nextTab = new QAction(this);
    nextTab->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_PageDown),
                           QKeySequence(Qt::CTRL | Qt::Key_Tab)});
    connect(nextTab, &QAction::triggered, this, [this]() {
        int n = m_tabs->count();
        if (n > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % n);
    });
    addAction(nextTab);

    QAction *prevTab = new QAction(this);
    prevTab->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_PageUp),
                           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),
                           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab)});
    connect(prevTab, &QAction::triggered, this, [this]() {
        int n = m_tabs->count();
        if (n > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() - 1 + n) % n);
    });
    addAction(prevTab);

    QAction *focusPage = new QAction(m_address);
    focusPage->setShortcut(QKeySequence(Qt::Key_Escape));
    focusPage->setShortcutContext(Qt::WidgetShortcut);
    connect(focusPage, &QAction::triggered, this, [this]() {
        if (ProcView *view = currentView())
            view->setFocus();
    });
    m_address->addAction(focusPage);

    QAction *quitAct = new QAction(this);
    quitAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    connect(quitAct, &QAction::triggered, this, [this]() { close(); });
    addAction(quitAct);

    updateNavActions();
}

ProcWindow::~ProcWindow() {
    if (m_bookmarks)
        ns_bookmarks_free(m_bookmarks);
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
            setWindowTitle(label + QStringLiteral(" — Nordstjernen"));
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
            setLoadingUi(loading);
    });
    connect(view, &ProcView::historyChanged, this, [this, view]() {
        if (view == currentView())
            updateNavActions();
    });
    connect(view, &ProcView::linkRequestedInNewTab, this,
            [this](const QString &url) { addTab(url, false); });
    connect(view, &ProcView::downloadRequested, this,
            &ProcWindow::startDownload);
}

void ProcWindow::updateChrome() {
    ProcView *view = currentView();
    if (!view) {
        m_address->clear();
        setWindowTitle(QStringLiteral("Nordstjernen"));
        setLoadingUi(false);
        updateNavActions();
        return;
    }
    setLoadingUi(view->isLoading());
    m_address->setText(view->currentUrl());
    const QString title = view->currentTitle();
    setWindowTitle((title.isEmpty() ? QStringLiteral("Nordstjernen")
                                    : title) +
                   QStringLiteral(" — Nordstjernen"));
    updateNavActions();
}

void ProcWindow::updateNavActions() {
    ProcView *view = currentView();
    m_backAction->setEnabled(view && view->canGoBack());
    m_forwardAction->setEnabled(view && view->canGoForward());
    m_reloadAction->setEnabled(view != nullptr);
}

void ProcWindow::setLoadingUi(bool loading) {
    m_spinner->setVisible(loading);
    ProcView *view = currentView();
    m_reloadAction->setEnabled(view && !loading);
}

QString ProcWindow::normalizeUrl(const QString &input) const {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return trimmed;

    if (trimmed.startsWith(QStringLiteral("about:")) ||
        trimmed.startsWith(QStringLiteral("file:")) ||
        trimmed.startsWith(QStringLiteral("data:")) ||
        trimmed.contains(QStringLiteral("://")))
        return trimmed;

    const QByteArray utf8 = trimmed.toUtf8();
    if (char *local = ns_url_from_local_path(utf8.constData())) {
        const QString out = QString::fromUtf8(local);
        g_free(local);
        return out;
    }
    if (ns_address_is_search(utf8.constData())) {
        char *url = ns_search_url_for(utf8.constData());
        const QString out = QString::fromUtf8(url);
        g_free(url);
        return out;
    }

    return QStringLiteral("https://") + trimmed;
}

bool ProcWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == m_address && event->type() == QEvent::FocusIn) {
        QTimer::singleShot(0, m_address, [this]() {
            if (m_address)
                m_address->selectAll();
        });
    }
    return QMainWindow::eventFilter(obj, event);
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
    const QString home =
        m_homeUrl.isEmpty() ? QStringLiteral("about:start") : m_homeUrl;
    ProcView *view = currentView();
    if (!view) {
        addTab(home);
        return;
    }
    view->load(home);
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
        "<p>This frontend uses the <a href=\"https://www.qt.io\">Qt</a> %1 "
        "framework, dynamically linked under the GNU LGPL v3. Qt is a "
        "trademark of The Qt Company Ltd; its source is available from "
        "<a href=\"https://download.qt.io\">download.qt.io</a>.</p>")
        .arg(QStringLiteral(QT_VERSION_STR));
    body += QStringLiteral(
        "<p>Includes other third-party open-source software; see "
        "THIRD-PARTY-LICENSES.md for full notices.</p>"
        "<p><a href=\"https://nordstjernen.org\">nordstjernen.org</a></p>"
        "<p>Nordstjernen Source License v1.0 — © 2026 Andreas Røsdal</p>");
    QMessageBox::about(this, QStringLiteral("About Nordstjernen"), body);
}

void ProcWindow::bookmarkCurrentPage() {
    ProcView *view = currentView();
    if (!view || !m_bookmarks)
        return;
    const QByteArray url = view->currentUrl().toUtf8();
    const QByteArray title = view->currentTitle().toUtf8();
    if (!url.isEmpty() && !ns_bookmarks_contains(m_bookmarks, url.constData())) {
        ns_bookmarks_add(m_bookmarks, url.constData(), title.constData());
        statusBar()->showMessage(QStringLiteral("Bookmark added"));
    }
}

void ProcWindow::rebuildBookmarksMenu() {
    m_bookmarksMenu->clear();
    QAction *add =
        m_bookmarksMenu->addAction(QStringLiteral("Bookmark this page"));
    connect(add, &QAction::triggered, this, &ProcWindow::bookmarkCurrentPage);
    m_bookmarksMenu->addSeparator();

    const guint n = m_bookmarks ? ns_bookmarks_count(m_bookmarks) : 0;
    if (n == 0) {
        QAction *empty =
            m_bookmarksMenu->addAction(QStringLiteral("No bookmarks yet"));
        empty->setEnabled(false);
        return;
    }

    QMenu *removeMenu =
        new QMenu(QStringLiteral("Remove bookmark"), m_bookmarksMenu);
    for (guint i = 0; i < n; ++i) {
        const ns_bookmark *bm = ns_bookmarks_get(m_bookmarks, i);
        if (!bm || !bm->url)
            continue;
        const QString url = QString::fromUtf8(bm->url);
        const QString label =
            (bm->title && *bm->title) ? QString::fromUtf8(bm->title) : url;
        QAction *open = m_bookmarksMenu->addAction(label.left(60));
        open->setToolTip(url);
        connect(open, &QAction::triggered, this, [this, url]() {
            if (ProcView *view = currentView())
                view->load(url);
            else
                addTab(url);
        });
        QAction *del = removeMenu->addAction(label.left(60));
        connect(del, &QAction::triggered, this, [this, url]() {
            if (m_bookmarks)
                ns_bookmarks_remove(m_bookmarks, url.toUtf8().constData());
        });
    }
    m_bookmarksMenu->addSeparator();
    m_bookmarksMenu->addMenu(removeMenu);
}

void ProcWindow::onSettings() {
    const ns_config *cfg = ns_config_get();

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Settings"));
    dlg.setMinimumWidth(460);

    QVBoxLayout *box = new QVBoxLayout(&dlg);
    QFormLayout *form = new QFormLayout();

    QLineEdit *home = new QLineEdit(&dlg);
    home->setText(cfg && cfg->home_url ? QString::fromUtf8(cfg->home_url)
                                       : QString());
    form->addRow(QStringLiteral("Home page"), home);

    const QString curEngine = cfg && cfg->search_engine
        ? QString::fromUtf8(cfg->search_engine) : QString();
    int match = kSearchEngineCount;
    for (int i = 0; i < kSearchEngineCount; ++i)
        if (curEngine == QLatin1String(kSearchEngines[i].url)) {
            match = i;
            break;
        }

    QComboBox *engines = new QComboBox(&dlg);
    for (int i = 0; i < kSearchEngineCount; ++i)
        engines->addItem(QString::fromUtf8(kSearchEngines[i].name));
    engines->addItem(QStringLiteral("Custom…"));
    engines->setCurrentIndex(match);
    form->addRow(QStringLiteral("Search engine"), engines);

    QLineEdit *custom = new QLineEdit(&dlg);
    custom->setPlaceholderText(
        QStringLiteral("https://example.com/search?q=%s"));
    custom->setText(curEngine);
    custom->setEnabled(match >= kSearchEngineCount);
    form->addRow(QStringLiteral("Custom URL"), custom);

    connect(engines, &QComboBox::currentIndexChanged, &dlg, [custom](int idx) {
        if (idx < kSearchEngineCount) {
            custom->setText(QLatin1String(kSearchEngines[idx].url));
            custom->setEnabled(false);
        } else {
            custom->setEnabled(true);
            custom->setFocus();
        }
    });

    QSpinBox *fontSize = new QSpinBox(&dlg);
    fontSize->setRange(8, 32);
    fontSize->setValue(cfg ? cfg->default_font_size_px : 16);
    form->addRow(QStringLiteral("Default font size"), fontSize);

    QCheckBox *images = new QCheckBox(QStringLiteral("Load images"), &dlg);
    images->setChecked(cfg ? cfg->images_enabled : true);
    form->addRow(images);
    QCheckBox *webgl = new QCheckBox(QStringLiteral("Enable WebGL"), &dlg);
    webgl->setChecked(cfg ? cfg->webgl_enabled : false);
    form->addRow(webgl);
    QCheckBox *storage =
        new QCheckBox(QStringLiteral("Enable local storage"), &dlg);
    storage->setChecked(cfg ? cfg->local_storage_enabled : true);
    form->addRow(storage);
    QCheckBox *dnt = new QCheckBox(QStringLiteral("Send Do Not Track"), &dlg);
    dnt->setChecked(cfg ? cfg->do_not_track : false);
    form->addRow(dnt);
    QCheckBox *cache = new QCheckBox(QStringLiteral("Enable cache"), &dlg);
    cache->setChecked(cfg ? cfg->cache_enabled : true);
    form->addRow(cache);

    box->addLayout(form);

    QLabel *note =
        new QLabel(QStringLiteral("Changes apply to newly opened pages."),
                   &dlg);
    note->setEnabled(false);
    box->addWidget(note);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    QPushButton *cancel = new QPushButton(QStringLiteral("Cancel"), &dlg);
    QPushButton *save = new QPushButton(QStringLiteral("Save"), &dlg);
    save->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    box->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(save, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted)
        return;

    ns_config *mut = ns_config_mut();
    if (!mut)
        return;
    const auto setStr = [](char **slot, const QString &value) {
        g_free(*slot);
        *slot = g_strdup(value.toUtf8().constData());
    };
    setStr(&mut->home_url, home->text());
    setStr(&mut->search_engine, custom->text());
    mut->default_font_size_px = fontSize->value();
    mut->images_enabled = images->isChecked();
    mut->webgl_enabled = webgl->isChecked();
    mut->local_storage_enabled = storage->isChecked();
    mut->do_not_track = dnt->isChecked();
    mut->cache_enabled = cache->isChecked();
    ns_config_save(nullptr);
    m_homeUrl = configuredHomeUrl();
}

void ProcWindow::refreshTaskManager() {
    if (!m_taskTable)
        return;
    const int n = m_tabs ? m_tabs->count() : 0;
    m_taskTable->setRowCount(n);
    for (int i = 0; i < n; ++i) {
        ProcView *view = viewAt(i);
        int pid = view ? view->rendererPid() : -1;
        char state[32] = "starting";
        long rss = -1;
        if (pid > 0) {
            ns_rproc_http_proc_info(pid, state, sizeof state, &rss);
        } else if (view && ns_rproc_single_process_enabled()) {
            pid = ns_rproc_self_pid();
            ns_rproc_http_proc_info(pid, state, sizeof state, &rss);
            qstrncpy(state, "in-process", sizeof state);
        }

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

static QWidget *download_recent_row(const QString &name, const QString &path) {
    QWidget *row = new QWidget;
    QVBoxLayout *rl = new QVBoxLayout(row);
    rl->setContentsMargins(8, 6, 8, 6);
    QLabel *label = new QLabel(name);
    QHBoxLayout *hb = new QHBoxLayout;
    QPushButton *open = new QPushButton(QStringLiteral("Open"));
    QObject::connect(open, &QPushButton::clicked, open, [path]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    hb->addStretch(1);
    hb->addWidget(open);
    rl->addWidget(label);
    rl->addLayout(hb);
    return row;
}

void ProcWindow::ensureDownloadsDialog() {
    if (m_downloadsDialog)
        return;
    m_downloadsDialog = new QDialog(this);
    m_downloadsDialog->setWindowTitle(QStringLiteral("Downloads"));
    m_downloadsDialog->resize(460, 420);
    QVBoxLayout *outer = new QVBoxLayout(m_downloadsDialog);
    QHBoxLayout *header = new QHBoxLayout;
    header->addStretch(1);
    QPushButton *folder = new QPushButton(QStringLiteral("Open folder"));
    connect(folder, &QPushButton::clicked, this, []() {
        QString dir = QStandardPaths::writableLocation(
            QStandardPaths::DownloadLocation);
        if (dir.isEmpty())
            dir = QDir::homePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    header->addWidget(folder);
    outer->addLayout(header);

    QScrollArea *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    QWidget *container = new QWidget;
    m_downloadsLayout = new QVBoxLayout(container);

    QString dir = QStandardPaths::writableLocation(
        QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    const QFileInfoList recent =
        QDir(dir).entryInfoList(QDir::Files, QDir::Time);
    for (int i = 0; i < recent.size() && i < 25; i++)
        m_downloadsLayout->addWidget(
            download_recent_row(recent.at(i).fileName(),
                                recent.at(i).absoluteFilePath()));
    m_downloadsLayout->addStretch(1);

    scroll->setWidget(container);
    outer->addWidget(scroll, 1);
}

void ProcWindow::showDownloads() {
    ensureDownloadsDialog();
    m_downloadsDialog->show();
    m_downloadsDialog->raise();
    m_downloadsDialog->activateWindow();
}

void ProcWindow::startDownload(const QString &url, const QString &filename) {
    if (url.isEmpty())
        return;
    QString name = filename;
    if (name.isEmpty())
        name = QUrl(url).fileName();
    if (name.isEmpty())
        name = QStringLiteral("download");

    QString dir = QStandardPaths::writableLocation(
        QStandardPaths::DownloadLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    QString path = QDir(dir).filePath(name);
    for (int n = 1; QFile::exists(path) && n < 1000; n++)
        path = QDir(dir).filePath(QStringLiteral("%1.%2").arg(name).arg(n));

    ensureDownloadsDialog();
    QWidget *row = new QWidget;
    QVBoxLayout *rl = new QVBoxLayout(row);
    rl->setContentsMargins(8, 6, 8, 6);
    QLabel *status = new QLabel(name);
    QProgressBar *bar = new QProgressBar;
    bar->setRange(0, 0);
    QPushButton *open = new QPushButton(QStringLiteral("Open"));
    open->setEnabled(false);
    QHBoxLayout *hb = new QHBoxLayout;
    hb->addWidget(bar, 1);
    hb->addWidget(open);
    rl->addWidget(status);
    rl->addLayout(hb);
    m_downloadsLayout->insertWidget(0, row);
    showDownloads();

    QPointer<ProcWindow> self = this;
    const QString u = url;
    const QString p = path;
    std::thread([self, u, p, name, status, bar, open]() {
        GError *err = nullptr;
        ns_response *resp =
            ns_net_fetch_blocking(u.toUtf8().constData(), nullptr, &err);
        bool ok = false;
        qint64 size = 0;
        if (resp && !resp->error && resp->body) {
            QFile f(p);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(reinterpret_cast<const char *>(resp->body->data),
                        static_cast<qint64>(resp->body->len));
                f.close();
                ok = true;
                size = static_cast<qint64>(resp->body->len);
            }
        }
        if (resp)
            ns_response_free(resp);
        if (err)
            g_error_free(err);
        QMetaObject::invokeMethod(qApp, [self, status, bar, open, name, p,
                                         ok, size]() {
            if (!self)
                return;
            bar->setRange(0, 1);
            bar->setValue(ok ? 1 : 0);
            if (ok) {
                status->setText(QStringLiteral("%1 — %2 bytes")
                                    .arg(name).arg(size));
                open->setEnabled(true);
                QObject::connect(open, &QPushButton::clicked, open, [p]() {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(p));
                });
            } else {
                status->setText(QStringLiteral("%1 — Failed").arg(name));
            }
        }, Qt::QueuedConnection);
    }).detach();
}
