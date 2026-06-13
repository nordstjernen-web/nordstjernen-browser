/* Nordstjernen — Qt tabbed browser window with one renderer process per tab.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NORDSTJERNEN_QT_PROCWINDOW_H
#define NORDSTJERNEN_QT_PROCWINDOW_H

#include <QMainWindow>
#include <QString>

typedef struct ns_bookmarks ns_bookmarks;

class ProcView;
class QLineEdit;
class QMenu;
class QProgressBar;
class QTabBar;
class QStackedWidget;
class QAction;
class QDialog;
class QTableWidget;
class QVBoxLayout;

class ProcWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ProcWindow(QWidget *parent = nullptr);
    ~ProcWindow() override;

    ProcView *addTab(const QString &url, bool foreground = true);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onAddressEntered();
    void onBack();
    void onForward();
    void onReload();
    void onHome();
    void onNewTab();
    void onCloseTab(int index);
    void onCurrentChanged(int index);
    void onAbout();
    void onSettings();
    void onTaskManager();

    void startDownload(const QString &url, const QString &filename);
    void showDownloads();

private:
    ProcView *currentView() const;
    ProcView *viewAt(int index) const;
    void connectView(ProcView *view);
    void updateChrome();
    void updateNavActions();
    void setLoadingUi(bool loading);
    void rebuildBookmarksMenu();
    void bookmarkCurrentPage();
    void refreshTaskManager();
    void ensureDownloadsDialog();
    QString normalizeUrl(const QString &input) const;

    QTabBar *m_tabBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLineEdit *m_address = nullptr;
    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_homeAction = nullptr;
    QProgressBar *m_spinner = nullptr;
    QMenu *m_bookmarksMenu = nullptr;
    QDialog *m_taskMgr = nullptr;
    QTableWidget *m_taskTable = nullptr;
    QDialog *m_downloadsDialog = nullptr;
    QVBoxLayout *m_downloadsLayout = nullptr;
    QString m_homeUrl;
    ns_bookmarks *m_bookmarks = nullptr;
};

#endif
