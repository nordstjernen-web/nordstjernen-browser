/* Nordstjernen — Qt view backed by the out-of-process renderer (thin client).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#ifndef NORDSTJERNEN_QT_PROCVIEW_H
#define NORDSTJERNEN_QT_PROCVIEW_H

#include <QAbstractScrollArea>
#include <QIcon>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QStringList>

class QThread;
class ProcWorker;
class QLineEdit;
class QLabel;
class QWidget;
class QPlainTextEdit;
class QTimer;

class ProcView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit ProcView(QWidget *parent = nullptr);
    ~ProcView() override;

    void load(const QString &url);
    void back();
    void forward();
    void reload();
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void toggleConsole();
    void savePageAs(bool pdf);

    bool canGoBack() const;
    bool canGoForward() const;
    bool isLoading() const { return m_isLoading; }
    QString currentUrl() const { return m_currentUrl; }
    QString currentTitle() const { return m_currentTitle; }

    // Task-manager support: this tab's renderer OS pid (-1 if none), and a
    // forceful "End task" that kills the renderer (the tab respawns on next use).
    int rendererPid() const;
    void endTask();

signals:
    void titleChanged(const QString &title);
    void urlChanged(const QString &url);
    void statusMessage(const QString &message);
    void loadingChanged(bool loading);
    void historyChanged();
    void linkRequestedInNewTab(const QString &url);
    void downloadRequested(const QString &url, const QString &filename);
    void faviconChanged(const QIcon &icon);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool focusNextPrevChild(bool next) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void wheelEvent(QWheelEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class LinkAction { Hover, Navigate, NewTab };

    void doLoad(const QString &url, bool record);
    void promptWebgl(const QString &origin);
    void pushHistory(const QString &url);
    void requestRender();
    void startRender();
    void requestFavicon();
    void requestLinkAt(int x, int y, LinkAction action);
    void startLinkAt(int x, int y, LinkAction action);
    void requestClick(int x, int y, int mods);
    void requestViewport();
    void startViewport(int width, int height);
    void requestHover(int x, int y);
    void startHover(int x, int y);
    void requestSelect(int kind, int x, int y);
    void startSelect(int kind, int x, int y);
    void copySelection();
    void selectAll();
    void sendKey(int kind, QKeyEvent *event);
    void sendKeyText(const QString &text);
    void updateScrollRanges();
    void setZoom(double zoom);
    QString blockingLinkAt(int x, int y);
    void ensureFindBar();
    void openFindBar();
    void closeFindBar();
    void runFind(int direction);
    void ensureConsole();
    void pollConsole();
    void runEval();
    bool maybeLaunchMedia(int x, int y);
    void setLoading(bool loading);

    ProcWorker *m_worker = nullptr;
    QThread *m_workerThread = nullptr;
    QString m_rendererPath;
    QString m_currentUrl;
    QString m_currentTitle;
    bool m_isLoading = false;
    QStringList m_history;
    int m_historyIndex = -1;
    bool m_pendingRecord = true;
    int m_pageWidth = 0;
    int m_pageHeight = 0;
    double m_zoom = 1.0;
    bool m_opened = false;
    bool m_recoveringRender = false;
    int m_renderRestarts = 0;
    int m_jsRedirects = 0;
    bool m_renderInFlight = false;
    bool m_renderPending = false;
    bool m_webglPrompting = false;
    bool m_linkInFlight = false;
    bool m_linkPending = false;
    LinkAction m_linkPendingAction = LinkAction::Hover;
    int m_linkPendingX = 0;
    int m_linkPendingY = 0;
    bool m_hoverInFlight = false;
    bool m_hoverPending = false;
    int m_hoverPendingX = 0;
    int m_hoverPendingY = 0;
    bool m_selectInFlight = false;
    bool m_selectPending = false;
    int m_selectPendingKind = 0;
    int m_selectPendingX = 0;
    int m_selectPendingY = 0;
    bool m_mouseDown = false;
    bool m_dragAnchored = false;
    bool m_hasSelection = false;
    int m_dragStartX = 0;
    int m_dragStartY = 0;
    QPointF m_pressPos;
    int m_loadSeq = 0;
    int m_renderSeq = 0;
    int m_linkSeq = 0;
    int m_linkActiveSeq = 0;
    int m_clickSeq = 0;
    int m_keySeq = 0;
    int m_viewportSeq = 0;
    int m_hoverSeq = 0;
    int m_selectSeq = 0;
    int m_lastViewportWidth = 0;
    int m_lastViewportHeight = 0;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLabel *m_findLabel = nullptr;
    QWidget *m_console = nullptr;
    QPlainTextEdit *m_consoleLog = nullptr;
    QLineEdit *m_consoleInput = nullptr;
    QTimer *m_consoleTimer = nullptr;
    bool m_consoleOpen = false;
    QImage m_image;
};

#endif
