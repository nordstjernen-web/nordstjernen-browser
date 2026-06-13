/* Nordstjernen — Qt view backed by the out-of-process renderer (thin client). */

#include "procview.h"

#include "media.h"

extern "C" {
#include "proc_limits.h"
#include "rproc_http.h"
}

#include <QClipboard>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPixmap>
#include <QLabel>
#include <QRect>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <atomic>
#include <cstdlib>
#include <utility>


static int settleMs() {
    bool ok = false;
    int v = qEnvironmentVariableIntValue(NS_PROC_SETTLE_ENV, &ok);
    if (ok && v >= 0 && v <= 10000)
        return v;
    return NS_PROC_SETTLE_MS;
}

static int qtMods(Qt::KeyboardModifiers mods) {
    return ((mods & Qt::ShiftModifier) ? 1 : 0) |
           ((mods & Qt::ControlModifier) ? 2 : 0) |
           ((mods & Qt::AltModifier) ? 4 : 0) |
           ((mods & Qt::MetaModifier) ? 8 : 0);
}

static QString eventKey(const QKeyEvent *event) {
    const QString text = event->text();
    if (!text.isEmpty() && text.at(0).unicode() >= 0x20 &&
        text.at(0).unicode() != 0x7f)
        return text;
    switch (event->key()) {
    case Qt::Key_Up:        return QStringLiteral("ArrowUp");
    case Qt::Key_Down:      return QStringLiteral("ArrowDown");
    case Qt::Key_Left:      return QStringLiteral("ArrowLeft");
    case Qt::Key_Right:     return QStringLiteral("ArrowRight");
    case Qt::Key_Return:
    case Qt::Key_Enter:     return QStringLiteral("Enter");
    case Qt::Key_Escape:    return QStringLiteral("Escape");
    case Qt::Key_Backspace: return QStringLiteral("Backspace");
    case Qt::Key_Tab:
    case Qt::Key_Backtab:   return QStringLiteral("Tab");
    case Qt::Key_Delete:    return QStringLiteral("Delete");
    case Qt::Key_Insert:    return QStringLiteral("Insert");
    case Qt::Key_Home:      return QStringLiteral("Home");
    case Qt::Key_End:       return QStringLiteral("End");
    case Qt::Key_PageUp:    return QStringLiteral("PageUp");
    case Qt::Key_PageDown:  return QStringLiteral("PageDown");
    case Qt::Key_Shift:     return QStringLiteral("Shift");
    case Qt::Key_Control:   return QStringLiteral("Control");
    case Qt::Key_Alt:       return QStringLiteral("Alt");
    default:                return QString();
    }
}

static QString eventCode(const QKeyEvent *event) {
    const int key = event->key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QStringLiteral("Key%1").arg(QChar('A' + key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QStringLiteral("Digit%1").arg(QChar('0' + key - Qt::Key_0));
    switch (key) {
    case Qt::Key_Up:        return QStringLiteral("ArrowUp");
    case Qt::Key_Down:      return QStringLiteral("ArrowDown");
    case Qt::Key_Left:      return QStringLiteral("ArrowLeft");
    case Qt::Key_Right:     return QStringLiteral("ArrowRight");
    case Qt::Key_Return:    return QStringLiteral("Enter");
    case Qt::Key_Enter:     return QStringLiteral("NumpadEnter");
    case Qt::Key_Escape:    return QStringLiteral("Escape");
    case Qt::Key_Backspace: return QStringLiteral("Backspace");
    case Qt::Key_Tab:
    case Qt::Key_Backtab:   return QStringLiteral("Tab");
    case Qt::Key_Delete:    return QStringLiteral("Delete");
    case Qt::Key_Home:      return QStringLiteral("Home");
    case Qt::Key_End:       return QStringLiteral("End");
    case Qt::Key_PageUp:    return QStringLiteral("PageUp");
    case Qt::Key_PageDown:  return QStringLiteral("PageDown");
    case Qt::Key_Space:     return QStringLiteral("Space");
    default:                return QString();
    }
}

static int eventKeyCode(const QKeyEvent *event) {
    const int key = event->key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return 65 + key - Qt::Key_A;
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return 48 + key - Qt::Key_0;
    switch (key) {
    case Qt::Key_Backspace: return 8;
    case Qt::Key_Tab:       return 9;
    case Qt::Key_Return:
    case Qt::Key_Enter:     return 13;
    case Qt::Key_Escape:    return 27;
    case Qt::Key_Space:     return 32;
    case Qt::Key_PageUp:    return 33;
    case Qt::Key_PageDown:  return 34;
    case Qt::Key_End:       return 35;
    case Qt::Key_Home:      return 36;
    case Qt::Key_Left:      return 37;
    case Qt::Key_Up:        return 38;
    case Qt::Key_Right:     return 39;
    case Qt::Key_Down:      return 40;
    case Qt::Key_Delete:    return 46;
    default:                return 0;
    }
}

static QString rendererPath() {
    QByteArray path = qgetenv(NS_PROC_RENDERER_ENV);
    if (!path.isEmpty())
        return QString::fromUtf8(path);

    QString renderer = QStringLiteral(NS_PROC_RENDERER_NAME);
#ifdef Q_OS_WIN
    renderer += QStringLiteral(".exe");
#endif
    const QString dir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        dir + QStringLiteral("/") + renderer,
        dir + QStringLiteral("/../") + renderer,
    };
    for (const QString &candidate : candidates) {
        QFileInfo info(candidate);
        if (info.isExecutable())
            return info.absoluteFilePath();
    }

    return renderer;
}

struct PageResult {
    bool ok = false;
    int pageWidth = 0;
    int pageHeight = 0;
    QString title;
    QString url;
    QString nav;
};

struct FrameResult {
    bool ok = false;
    bool animating = false;
    bool unchanged = false;
    QImage image;
    QString nav;
    QString webgl;
    QString download;
};

class ProcWorker : public QObject {
public:
    explicit ProcWorker(QString rendererPath)
        : m_rendererPath(std::move(rendererPath)) {}

    ~ProcWorker() override {
        closeProcess();
    }

    /* Lock-free read of the renderer pid for the GUI-thread task manager. */
    int pid() const { return m_pid.load(std::memory_order_relaxed); }

    PageResult load(const QString &url, int viewportWidth,
                    int viewportHeight) {
        PageResult result;
        if (url.isEmpty())
            return result;
        if (!ensureProcess())
            return result;
        if (!openPage(url, viewportWidth, viewportHeight, &result) &&
            restartProcess())
            openPage(url, viewportWidth, viewportHeight, &result);
        return result;
    }

    FrameResult render(int width, int height, int scrollX, int scrollY,
                       double scale, double imageDpr) {
        FrameResult result;
        if (!m_proc || !m_opened)
            return result;
        ns_rproc_http_frame fr;
        if (ns_rproc_http_render(m_proc, width, height, scrollX, scrollY, scale,
                            &fr) != 0) {
            closeProcess();
            return result;
        }
        if (!fr.ok)
            return result;
        result.unchanged = fr.unchanged != 0;
        if (!result.unchanged) {
            QImage img(const_cast<uchar *>(fr.pixels), fr.width, fr.height,
                       fr.stride, QImage::Format_ARGB32_Premultiplied);
            result.image = img.copy();
            result.image.setDevicePixelRatio(imageDpr);
        }
        result.animating = fr.animating != 0;
        if (fr.nav) {
            result.nav = QString::fromUtf8(fr.nav);
            free(fr.nav);
        }
        if (fr.webgl) {
            result.webgl = QString::fromUtf8(fr.webgl);
            free(fr.webgl);
        }
        if (fr.download) {
            result.download = QString::fromUtf8(fr.download);
            free(fr.download);
        }
        result.ok = true;
        return result;
    }

    void resolveWebgl(const QString &origin, bool allow) {
        if (!m_proc || origin.isEmpty())
            return;
        ns_rproc_http_resolve_webgl(m_proc, origin.toUtf8().constData(),
                                    allow ? 1 : 0);
    }

    QImage favicon() {
        if (!m_proc || !m_opened)
            return QImage();
        int w = 0, h = 0, stride = 0;
        unsigned char *px = ns_rproc_http_favicon(m_proc, &w, &h, &stride);
        if (!px)
            return QImage();
        QImage img(px, w, h, stride, QImage::Format_ARGB32_Premultiplied);
        QImage copy = img.copy();
        free(px);
        return copy;
    }

    QString linkAt(int x, int y) {
        if (!m_proc || !m_opened)
            return QString();
        char *href = ns_rproc_http_link_at(m_proc, x, y);
        if (!href)
            return QString();
        QString result = QString::fromUtf8(href);
        free(href);
        return result;
    }

    QString linkCursorAt(int x, int y, QString *cursorOut) {
        if (cursorOut)
            cursorOut->clear();
        if (!m_proc || !m_opened)
            return QString();
        char *cursor = nullptr;
        char *href = ns_rproc_http_link_cursor_at(m_proc, x, y, &cursor);
        if (cursor) {
            if (cursorOut)
                *cursorOut = QString::fromUtf8(cursor);
            free(cursor);
        }
        if (!href)
            return QString();
        QString result = QString::fromUtf8(href);
        free(href);
        return result;
    }

    bool hover(int x, int y, QString *hrefOut, QString *cursorOut) {
        if (hrefOut)
            hrefOut->clear();
        if (cursorOut)
            cursorOut->clear();
        if (!m_proc || !m_opened)
            return false;
        char *href = nullptr;
        char *cursor = nullptr;
        int changed = ns_rproc_http_hover_full(m_proc, x, y, &href, &cursor);
        if (href) {
            if (hrefOut)
                *hrefOut = QString::fromUtf8(href);
            free(href);
        }
        if (cursor) {
            if (cursorOut)
                *cursorOut = QString::fromUtf8(cursor);
            free(cursor);
        }
        return changed == 1;
    }

    bool release() {
        if (!m_proc || !m_opened)
            return false;
        return ns_rproc_http_release(m_proc) == 1;
    }

    QString click(int x, int y, int mods) {
        if (!m_proc || !m_opened)
            return QString();
        char *href = ns_rproc_http_click(m_proc, x, y, mods);
        if (!href)
            return QString();
        QString result = QString::fromUtf8(href);
        free(href);
        return result;
    }

    QString key(int kind, const QString &key, const QString &code,
                int keycode, int mods) {
        if (!m_proc || !m_opened)
            return QString();
        QByteArray keyBytes = key.toUtf8();
        QByteArray codeBytes = code.toUtf8();
        char *href = ns_rproc_http_key(m_proc, kind, keyBytes.constData(),
                                  codeBytes.constData(), keycode, mods);
        if (!href)
            return QString();
        QString result = QString::fromUtf8(href);
        free(href);
        return result;
    }

    PageResult setViewport(int viewportWidth, int viewportHeight) {
        PageResult result;
        if (!m_proc || !m_opened)
            return result;
        ns_rproc_http_page page = {};
        if (ns_rproc_http_set_viewport(m_proc, qMax(1, viewportWidth),
                                  qMax(1, viewportHeight), &page) == 0 &&
            page.ok) {
            result.ok = true;
            result.pageWidth = page.page_width;
            result.pageHeight = page.page_height;
        }
        ns_rproc_http_page_clear(&page);
        return result;
    }

    void find(const QString &query, bool caseSensitive, int direction,
              int fromY, int *total, int *current, int *scrollY) {
        if (total) *total = 0;
        if (current) *current = 0;
        if (scrollY) *scrollY = 0;
        if (!m_proc || !m_opened)
            return;
        ns_rproc_http_find(m_proc, query.toUtf8().constData(),
                      caseSensitive ? 1 : 0, direction, fromY,
                      total, current, scrollY);
    }

    QString consolePoll() {
        if (!m_proc || !m_opened)
            return QString();
        char *log = ns_rproc_http_console_poll(m_proc);
        if (!log)
            return QString();
        QString r = QString::fromUtf8(log);
        free(log);
        return r;
    }

    QString eval(const QString &src) {
        if (!m_proc || !m_opened)
            return QString();
        char *res = ns_rproc_http_eval(m_proc, src.toUtf8().constData());
        if (!res)
            return QString();
        QString r = QString::fromUtf8(res);
        free(res);
        return r;
    }

    bool exportPage(const QString &path) {
        if (!m_proc || !m_opened)
            return false;
        return ns_rproc_http_export(m_proc, path.toUtf8().constData()) == 0;
    }

    QString selectText(int kind, int x, int y) {
        if (!m_proc || !m_opened)
            return QString();
        char *text = ns_rproc_http_select(m_proc, kind, x, y);
        if (!text)
            return QString();
        QString result = QString::fromUtf8(text);
        free(text);
        return result;
    }

    QString mediaAt(int x, int y, int *isVideo, int *stream) {
        if (!m_proc || !m_opened)
            return QString();
        char *u = ns_rproc_http_media_at(m_proc, x, y, isVideo, stream);
        if (!u)
            return QString();
        QString r = QString::fromUtf8(u);
        free(u);
        return r;
    }

    void closeProcess() {
        ns_rproc_http *proc;
        {
            QMutexLocker lock(&m_procMutex);
            proc = m_proc;
            m_proc = nullptr;
        }
        if (proc)
            ns_rproc_http_close(proc);
        m_pid.store(-1, std::memory_order_relaxed);
        m_opened = false;
    }

    /* Thread-safe: refuse new renderer connections and unblock any
       in-flight request before the GUI thread blocks on teardown. */
    void stop() {
        m_stopping.store(true, std::memory_order_relaxed);
        QMutexLocker lock(&m_procMutex);
        if (m_proc)
            ns_rproc_http_interrupt(m_proc);
    }

private:
    bool ensureProcess() {
        if (m_proc)
            return true;
        if (m_stopping.load(std::memory_order_relaxed))
            return false;
        QByteArray path = m_rendererPath.toUtf8();
        ns_rproc_http *proc = ns_rproc_http_spawn_shm(path.constData(), NS_PROC_MAX_WIDTH, NS_PROC_MAX_HEIGHT);
        {
            QMutexLocker lock(&m_procMutex);
            m_proc = proc;
        }
        m_pid.store(proc ? ns_rproc_http_pid(proc) : -1,
                    std::memory_order_relaxed);
        return proc != nullptr;
    }

    bool restartProcess() {
        closeProcess();
        return ensureProcess();
    }

    bool openPage(const QString &url, int viewportWidth, int viewportHeight,
                  PageResult *out) {
        if (!m_proc || !out)
            return false;
        ns_rproc_http_page page;
        const int width = qMax(1, viewportWidth);
        const int height = qMax(1, viewportHeight);
        bool ok = ns_rproc_http_open(m_proc, url.toUtf8().constData(), width,
                                height, settleMs(), &page) == 0 && page.ok;
        if (ok) {
            out->ok = true;
            out->pageWidth = page.page_width;
            out->pageHeight = page.page_height;
            out->title = page.title ? QString::fromUtf8(page.title)
                                    : QString();
            out->url = page.url ? QString::fromUtf8(page.url) : url;
            out->nav = page.nav ? QString::fromUtf8(page.nav) : QString();
            m_opened = true;
        }
        ns_rproc_http_page_clear(&page);
        if (!ok)
            m_opened = false;
        return ok;
    }

    QString m_rendererPath;
    ns_rproc_http *m_proc = nullptr;
    QMutex m_procMutex;
    std::atomic<bool> m_stopping{false};
    bool m_opened = false;
    std::atomic<int> m_pid{-1};
};

int ProcView::rendererPid() const {
    return m_worker ? m_worker->pid() : -1;
}

void ProcView::endTask() {
    ns_proc_kill(rendererPid());
}

ProcView::ProcView(QWidget *parent) : QAbstractScrollArea(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    viewport()->setAttribute(Qt::WA_InputMethodEnabled, true);
    viewport()->setMouseTracking(true);
    verticalScrollBar()->setSingleStep(40);
    horizontalScrollBar()->setSingleStep(40);

    m_rendererPath = rendererPath();
    m_workerThread = new QThread(this);
    m_worker = new ProcWorker(m_rendererPath);
    m_worker->moveToThread(m_workerThread);
    connect(m_workerThread, &QThread::finished, m_worker,
            &QObject::deleteLater);
    m_workerThread->start();
}

ProcView::~ProcView() {
    if (m_workerThread) {
        if (m_worker && m_workerThread->isRunning()) {
            m_worker->stop();
            QMetaObject::invokeMethod(m_worker, [worker = m_worker]() {
                worker->closeProcess();
            }, Qt::BlockingQueuedConnection);
        }
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void ProcView::load(const QString &url) {
    m_renderRestarts = 0;
    doLoad(url, true);
}

bool ProcView::canGoBack() const {
    return m_historyIndex > 0;
}

bool ProcView::canGoForward() const {
    return m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1;
}

void ProcView::back() {
    if (!canGoBack())
        return;
    m_historyIndex--;
    m_renderRestarts = 0;
    emit historyChanged();
    doLoad(m_history.at(m_historyIndex), false);
}

void ProcView::forward() {
    if (!canGoForward())
        return;
    m_historyIndex++;
    m_renderRestarts = 0;
    emit historyChanged();
    doLoad(m_history.at(m_historyIndex), false);
}

void ProcView::reload() {
    m_renderRestarts = 0;
    if (m_historyIndex >= 0 && m_historyIndex < m_history.size())
        doLoad(m_history.at(m_historyIndex), false);
    else if (!m_currentUrl.isEmpty())
        doLoad(m_currentUrl, false);
}

void ProcView::setLoading(bool loading) {
    m_isLoading = loading;
    emit loadingChanged(loading);
}

void ProcView::pushHistory(const QString &url) {
    if (url.isEmpty())
        return;
    if (m_historyIndex >= 0 && m_history.at(m_historyIndex) == url)
        return;
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(url);
    m_historyIndex = m_history.size() - 1;
    emit historyChanged();
}

void ProcView::doLoad(const QString &url, bool record) {
    if (url.isEmpty() || !m_worker)
        return;

    if (record)
        m_jsRedirects = 0;
    m_pendingRecord = record;
    setLoading(true);
    viewport()->setCursor(Qt::BusyCursor);
    emit statusMessage(QStringLiteral("Loading %1…").arg(url));
    const int seq = ++m_loadSeq;
    ++m_renderSeq;
    ++m_linkSeq;
    ++m_clickSeq;
    ++m_keySeq;
    ++m_viewportSeq;
    m_renderPending = false;
    m_linkInFlight = false;
    m_linkPending = false;
    m_linkPendingAction = LinkAction::Hover;
    m_linkActiveSeq = 0;
    ++m_hoverSeq;
    m_hoverInFlight = false;
    m_hoverPending = false;
    ++m_selectSeq;
    m_selectInFlight = false;
    m_selectPending = false;
    m_mouseDown = false;
    m_dragAnchored = false;
    m_hasSelection = false;
    m_opened = false;
    m_image = QImage();
    viewport()->update();

    QPointer<ProcView> self(this);
    ProcWorker *worker = m_worker;
    const QString requested = url;
    const int viewportWidth = viewport()->width();
    const int viewportHeight = viewport()->height();
    QMetaObject::invokeMethod(worker, [worker, self, seq, requested,
                                       viewportWidth, viewportHeight]() {
        PageResult result = worker->load(requested, viewportWidth,
                                         viewportHeight);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, seq, requested, result]() {
            if (!self || seq != self->m_loadSeq)
                return;
            self->m_recoveringRender = false;
            if (!result.ok) {
                self->setLoading(false);
                self->viewport()->setCursor(Qt::ArrowCursor);
                emit self->statusMessage(
                    QStringLiteral("Failed to load %1").arg(requested));
                return;
            }

            if (!result.nav.isEmpty() &&
                self->m_jsRedirects < NS_PROC_MAX_JS_REDIRECTS) {
                self->m_jsRedirects++;
                self->doLoad(result.nav, self->m_pendingRecord);
                return;
            }

            self->m_currentUrl = result.url.isEmpty() ? requested : result.url;
            self->m_currentTitle = result.title;
            self->m_pageWidth = result.pageWidth;
            self->m_pageHeight = result.pageHeight;
            self->m_lastViewportWidth = qMax(1, self->viewport()->width());
            self->m_lastViewportHeight = qMax(1, self->viewport()->height());
            self->m_opened = true;

            if (self->m_pendingRecord)
                self->pushHistory(self->m_currentUrl);

            emit self->urlChanged(self->m_currentUrl);
            emit self->titleChanged(result.title);
            self->setLoading(false);

            self->horizontalScrollBar()->setValue(0);
            self->verticalScrollBar()->setValue(0);
            self->updateScrollRanges();
            self->requestRender();
            self->requestFavicon();
            emit self->statusMessage(QStringLiteral("Done"));
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ProcView::requestFavicon() {
    if (!m_opened || !m_worker)
        return;
    QPointer<ProcView> self(this);
    ProcWorker *worker = m_worker;
    const int seq = m_loadSeq;
    QMetaObject::invokeMethod(worker, [worker, self, seq]() {
        QImage icon = worker->favicon();
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, seq, icon]() {
            if (!self || seq != self->m_loadSeq)
                return;
            emit self->faviconChanged(
                icon.isNull() ? QIcon()
                              : QIcon(QPixmap::fromImage(icon)));
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ProcView::promptWebgl(const QString &origin) {
    if (m_webglPrompting || !m_worker)
        return;
    m_webglPrompting = true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Enable WebGL?"));
    box.setText(tr("Enable WebGL for %1?").arg(origin));
    box.setInformativeText(
        tr("This page wants to use WebGL (hardware-accelerated 3D graphics) "
           "on %1.\n\nWebGL hands the page near-direct access to your GPU "
           "driver — only allow it on sites you trust. Allowing keeps WebGL "
           "enabled for this site for the rest of the session and reloads "
           "the page.").arg(origin));
    QPushButton *block = box.addButton(tr("Block"), QMessageBox::RejectRole);
    QPushButton *allow =
        box.addButton(tr("Allow and trust this site"), QMessageBox::AcceptRole);
    box.setDefaultButton(block);
    box.exec();
    bool allowed = box.clickedButton() == allow;

    if (allowed) {
        QMessageBox confirm(this);
        confirm.setIcon(QMessageBox::Warning);
        confirm.setWindowTitle(tr("Are you sure?"));
        confirm.setText(tr("Give %1 near-direct access to your GPU driver?")
                            .arg(origin));
        confirm.setInformativeText(
            tr("This stays enabled for this origin for the rest of the "
               "session."));
        QPushButton *cancel =
            confirm.addButton(tr("Cancel"), QMessageBox::RejectRole);
        confirm.addButton(tr("Enable WebGL"), QMessageBox::AcceptRole);
        confirm.setDefaultButton(cancel);
        confirm.exec();
        allowed = confirm.clickedButton() != cancel;
    }

    ProcWorker *worker = m_worker;
    const QString o = origin;
    const bool a = allowed;
    QMetaObject::invokeMethod(
        worker, [worker, o, a]() { worker->resolveWebgl(o, a); },
        Qt::QueuedConnection);

    m_webglPrompting = false;
    if (allowed && !m_currentUrl.isEmpty())
        doLoad(m_currentUrl, false);
}

void ProcView::updateScrollRanges() {
    const int vw = qMax(1, int(viewport()->width() / m_zoom));
    const int vh = qMax(1, int(viewport()->height() / m_zoom));
    horizontalScrollBar()->setRange(0, qMax(0, m_pageWidth - vw));
    horizontalScrollBar()->setPageStep(vw);
    verticalScrollBar()->setRange(0, qMax(0, m_pageHeight - vh));
    verticalScrollBar()->setPageStep(vh);
}

void ProcView::requestRender() {
    if (!m_opened || !m_worker)
        return;
    if (m_renderInFlight) {
        m_renderPending = true;
        return;
    }
    startRender();
}

void ProcView::startRender() {
    if (!m_opened || !m_worker)
        return;

    m_renderInFlight = true;
    const int seq = ++m_renderSeq;

    const qreal dpr = devicePixelRatioF();
    const double scale = dpr * m_zoom;
    const int w = qMax(1, int(viewport()->width() * dpr));
    const int h = qMax(1, int(viewport()->height() * dpr));
    const int sx = horizontalScrollBar()->value();
    const int sy = verticalScrollBar()->value();

    QPointer<ProcView> self(this);
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, self, seq, w, h, sx, sy, scale,
                                       dpr]() {
        FrameResult result = worker->render(w, h, sx, sy, scale, double(dpr));
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, seq, result]() {
            if (!self)
                return;
            const bool current = seq == self->m_renderSeq;
            if (current && result.ok && !result.unchanged) {
                self->m_image = result.image;
                self->m_renderRestarts = 0;
                self->viewport()->update();
            } else if (current && result.ok) {
                self->m_renderRestarts = 0;
            }
            if (current && result.ok && result.animating) {
                QPointer<ProcView> sp(self);
                QTimer::singleShot(16, self.data(), [sp]() {
                    if (sp && sp->m_opened)
                        sp->requestRender();
                });
            }
            self->m_renderInFlight = false;
            if (current && result.ok && !result.nav.isEmpty() &&
                self->m_jsRedirects < NS_PROC_MAX_JS_REDIRECTS) {
                self->m_jsRedirects++;
                self->doLoad(result.nav, false);
                return;
            }
            if (result.ok && !result.webgl.isEmpty())
                self->promptWebgl(result.webgl);
            if (result.ok && !result.download.isEmpty()) {
                const int tab = result.download.indexOf(QLatin1Char('\t'));
                const QString url = tab >= 0 ? result.download.left(tab)
                                             : result.download;
                const QString name = tab >= 0 ? result.download.mid(tab + 1)
                                              : QString();
                emit self->downloadRequested(url, name);
            }
            if (self->m_renderPending) {
                self->m_renderPending = false;
                self->startRender();
                return;
            }
            if (current && !result.ok && !self->m_currentUrl.isEmpty() &&
                !self->m_recoveringRender) {
                if (self->m_renderRestarts < NS_PROC_MAX_RESTARTS) {
                    self->m_renderRestarts++;
                    self->m_recoveringRender = true;
                    emit self->statusMessage(
                        QStringLiteral("Renderer restarted"));
                    self->doLoad(self->m_currentUrl, false);
                } else {
                    emit self->statusMessage(QStringLiteral(
                        "This tab's renderer keeps failing — press reload "
                        "to try again"));
                }
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ProcView::requestLinkAt(int x, int y, LinkAction action) {
    if (!m_opened || !m_worker)
        return;
    if (m_linkInFlight) {
        if (action != LinkAction::Hover ||
            m_linkPendingAction == LinkAction::Hover) {
            m_linkPendingX = x;
            m_linkPendingY = y;
            m_linkPendingAction = action;
        }
        m_linkPending = true;
        return;
    }
    startLinkAt(x, y, action);
}

static QCursor cursorFromCssKeyword(const QString &name, bool overLink) {
    if (name.isEmpty())
        return overLink ? QCursor(Qt::PointingHandCursor)
                        : QCursor(Qt::ArrowCursor);
    if (name == QLatin1String("pointer"))
        return QCursor(Qt::PointingHandCursor);
    if (name == QLatin1String("text") ||
        name == QLatin1String("vertical-text"))
        return QCursor(Qt::IBeamCursor);
    if (name == QLatin1String("wait"))
        return QCursor(Qt::WaitCursor);
    if (name == QLatin1String("progress"))
        return QCursor(Qt::BusyCursor);
    if (name == QLatin1String("crosshair") || name == QLatin1String("cell"))
        return QCursor(Qt::CrossCursor);
    if (name == QLatin1String("move") || name == QLatin1String("all-scroll"))
        return QCursor(Qt::SizeAllCursor);
    if (name == QLatin1String("not-allowed") ||
        name == QLatin1String("no-drop"))
        return QCursor(Qt::ForbiddenCursor);
    if (name == QLatin1String("help"))
        return QCursor(Qt::WhatsThisCursor);
    if (name == QLatin1String("grab"))
        return QCursor(Qt::OpenHandCursor);
    if (name == QLatin1String("grabbing"))
        return QCursor(Qt::ClosedHandCursor);
    if (name == QLatin1String("copy"))
        return QCursor(Qt::DragCopyCursor);
    if (name == QLatin1String("alias"))
        return QCursor(Qt::DragLinkCursor);
    if (name == QLatin1String("none"))
        return QCursor(Qt::BlankCursor);
    if (name == QLatin1String("col-resize") ||
        name == QLatin1String("ew-resize") ||
        name == QLatin1String("e-resize") || name == QLatin1String("w-resize"))
        return QCursor(Qt::SizeHorCursor);
    if (name == QLatin1String("row-resize") ||
        name == QLatin1String("ns-resize") ||
        name == QLatin1String("n-resize") || name == QLatin1String("s-resize"))
        return QCursor(Qt::SizeVerCursor);
    if (name == QLatin1String("nesw-resize") ||
        name == QLatin1String("ne-resize") ||
        name == QLatin1String("sw-resize"))
        return QCursor(Qt::SizeBDiagCursor);
    if (name == QLatin1String("nwse-resize") ||
        name == QLatin1String("nw-resize") ||
        name == QLatin1String("se-resize"))
        return QCursor(Qt::SizeFDiagCursor);
    return overLink ? QCursor(Qt::PointingHandCursor)
                    : QCursor(Qt::ArrowCursor);
}

void ProcView::startLinkAt(int x, int y, LinkAction action) {
    if (!m_opened || !m_worker)
        return;
    m_linkInFlight = true;
    const int seq = ++m_linkSeq;
    m_linkActiveSeq = seq;
    QPointer<ProcView> self(this);
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, self, seq, x, y, action]() {
        QString cssCursor;
        QString href = worker->linkCursorAt(x, y, &cssCursor);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, seq, x, y, href,
                                                cssCursor, action]() {
            if (!self || seq != self->m_linkSeq ||
                seq != self->m_linkActiveSeq)
                return;
            self->m_linkInFlight = false;
            self->m_linkActiveSeq = 0;
            bool navigated = false;
            self->viewport()->setCursor(
                cursorFromCssKeyword(cssCursor, !href.isEmpty()));
            if (!href.isEmpty()) {
                emit self->statusMessage(href);
                if (action == LinkAction::Navigate) {
                    navigated = true;
                    self->load(href);
                } else if (action == LinkAction::NewTab) {
                    emit self->linkRequestedInNewTab(href);
                }
            } else if (action == LinkAction::Navigate) {
                navigated = self->maybeLaunchMedia(x, y);
            }
            if (navigated) {
                self->m_linkPending = false;
                self->m_linkPendingAction = LinkAction::Hover;
                return;
            }
            if (self->m_linkPending) {
                const int pendingX = self->m_linkPendingX;
                const int pendingY = self->m_linkPendingY;
                const LinkAction pendingAction = self->m_linkPendingAction;
                self->m_linkPending = false;
                self->m_linkPendingAction = LinkAction::Hover;
                self->startLinkAt(pendingX, pendingY, pendingAction);
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ProcView::requestHover(int x, int y) {
    if (!m_opened || !m_worker)
        return;
    if (m_hoverInFlight) {
        m_hoverPendingX = x;
        m_hoverPendingY = y;
        m_hoverPending = true;
        return;
    }
    startHover(x, y);
}

void ProcView::startHover(int x, int y) {
    if (!m_opened || !m_worker)
        return;
    m_hoverInFlight = true;
    const int seq = ++m_hoverSeq;
    QPointer<ProcView> self(this);
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, self, seq, x, y]() {
        QString href;
        QString cssCursor;
        const bool changed = worker->hover(x, y, &href, &cssCursor);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, seq, changed, href,
                                                cssCursor]() {
            if (!self || seq != self->m_hoverSeq)
                return;
            self->m_hoverInFlight = false;
            self->viewport()->setCursor(
                cursorFromCssKeyword(cssCursor, !href.isEmpty()));
            if (!href.isEmpty())
                emit self->statusMessage(href);
            if (changed)
                self->requestRender();
            if (self->m_hoverPending) {
                const int px = self->m_hoverPendingX;
                const int py = self->m_hoverPendingY;
                self->m_hoverPending = false;
                self->startHover(px, py);
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ProcView::requestSelect(int kind, int x, int y) {
    if (!m_opened || !m_worker)
        return;
    if (m_selectInFlight) {
        m_selectPending = true;
        m_selectPendingKind = kind;
        m_selectPendingX = x;
        m_selectPendingY = y;
        return;
    }
    startSelect(kind, x, y);
}

void ProcView::startSelect(int kind, int x, int y) {
    if (!m_opened || !m_worker)
        return;
    m_selectInFlight = true;
    const int seq = ++m_selectSeq;
    QPointer<ProcView> self(this);
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, self, seq, kind, x, y]() {
        worker->selectText(kind, x, y);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, seq]() {
            if (!self || seq != self->m_selectSeq)
                return;
            self->m_selectInFlight = false;
            self->requestRender();
            if (self->m_selectPending) {
                const int kind = self->m_selectPendingKind;
                const int x = self->m_selectPendingX;
                const int y = self->m_selectPendingY;
                self->m_selectPending = false;
                self->startSelect(kind, x, y);
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ProcView::copySelection() {
    if (!m_opened || !m_worker)
        return;
    QString text;
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, &text]() { text = worker->selectText(4, 0, 0); },
        Qt::BlockingQueuedConnection);
    if (!text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
        emit statusMessage(QStringLiteral("Copied selection"));
    }
}

void ProcView::selectAll() {
    if (!m_opened || !m_worker)
        return;
    m_hasSelection = true;
    requestSelect(3, 0, 0);
}

void ProcView::requestClick(int x, int y, int mods) {
    if (!m_opened || !m_worker)
        return;
    const int seq = ++m_clickSeq;
    QPointer<ProcView> self(this);
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, self, seq, x, y, mods]() {
        QString href = worker->click(x, y, mods);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, seq, x, y, href]() {
            if (!self || seq != self->m_clickSeq)
                return;
            if (!href.isEmpty()) {
                emit self->statusMessage(href);
                self->load(href);
                return;
            }
            if (!self->maybeLaunchMedia(x, y))
                self->requestRender();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ProcView::requestViewport() {
    if (!m_opened || !m_worker)
        return;
    const int width = qMax(1, viewport()->width());
    const int height = qMax(1, viewport()->height());
    if (width <= 1 || height <= 1) {
        requestRender();
        return;
    }
    if (qAbs(width - m_lastViewportWidth) < 16 &&
        qAbs(height - m_lastViewportHeight) < 16) {
        requestRender();
        return;
    }
    m_lastViewportWidth = width;
    m_lastViewportHeight = height;
    startViewport(width, height);
}

void ProcView::startViewport(int width, int height) {
    if (!m_opened || !m_worker)
        return;
    const int seq = ++m_viewportSeq;
    QPointer<ProcView> self(this);
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, self, seq, width, height]() {
        PageResult result = worker->setViewport(width, height);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, seq, result]() {
            if (!self || seq != self->m_viewportSeq)
                return;
            if (result.ok) {
                self->m_pageWidth = result.pageWidth;
                self->m_pageHeight = result.pageHeight;
                self->updateScrollRanges();
            }
            self->requestRender();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ProcView::sendKey(int kind, QKeyEvent *event) {
    if (!m_opened || !m_worker)
        return;
    const QString key = eventKey(event);
    const QString code = eventCode(event);
    const int keycode = eventKeyCode(event);
    const int mods = qtMods(event->modifiers());
    const int seq = kind == 0 ? ++m_keySeq : m_keySeq;
    QPointer<ProcView> self(this);
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, self, seq, kind, key, code,
                                       keycode, mods]() {
        QString href = worker->key(kind, key, code, keycode, mods);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, seq, kind, href]() {
            if (!self || (kind == 0 && seq != self->m_keySeq))
                return;
            if (kind == 0 && !href.isEmpty()) {
                emit self->statusMessage(href);
                self->load(href);
            } else if (kind == 0) {
                self->requestRender();
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ProcView::sendKeyText(const QString &text) {
    if (!m_opened || !m_worker || text.isEmpty())
        return;
    QPointer<ProcView> self(this);
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, self, text]() {
        worker->key(2, text, QString(), 0, 0);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self]() {
            if (self)
                self->requestRender();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void ProcView::inputMethodEvent(QInputMethodEvent *event) {
    const QString commit = event->commitString();
    if (!commit.isEmpty())
        sendKeyText(commit);
    event->accept();
}

QVariant ProcView::inputMethodQuery(Qt::InputMethodQuery query) const {
    switch (query) {
    case Qt::ImEnabled:
        return QVariant(m_opened);
    case Qt::ImCursorRectangle:
        return QRect(0, viewport()->height() - 24, 1, 24);
    case Qt::ImHints:
        return QVariant(int(Qt::ImhNone));
    default:
        return QAbstractScrollArea::inputMethodQuery(query);
    }
}

void ProcView::paintEvent(QPaintEvent *event) {
    QPainter painter(viewport());
    painter.fillRect(event->rect(), Qt::white);
    if (!m_image.isNull())
        painter.drawImage(QPointF(0, 0), m_image);
}

void ProcView::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    if (m_findBar && m_findBar->isVisible())
        m_findBar->move(viewport()->width() - m_findBar->width() - 4, 4);
    if (m_opened) {
        updateScrollRanges();
        requestViewport();
    }
}

void ProcView::scrollContentsBy(int dx, int dy) {
    (void)dx;
    (void)dy;
    requestRender();
}

void ProcView::mousePressEvent(QMouseEvent *event) {
    const bool left = event->button() == Qt::LeftButton;
    const bool middle = event->button() == Qt::MiddleButton;
    if (m_opened && (left || middle)) {
        setFocus();
        const bool ctrl = event->modifiers() & Qt::ControlModifier;
        const int x = horizontalScrollBar()->value() +
                      int(event->position().x() / m_zoom);
        const int y = verticalScrollBar()->value() +
                      int(event->position().y() / m_zoom);
        if (middle || (left && ctrl)) {
            requestLinkAt(x, y, LinkAction::NewTab);
            return;
        }
        m_mouseDown = true;
        m_dragAnchored = false;
        m_dragStartX = x;
        m_dragStartY = y;
        m_pressPos = event->position();
        m_hasSelection = false;
        requestClick(x, y, qtMods(event->modifiers()));
        return;
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void ProcView::mouseMoveEvent(QMouseEvent *event) {
    if (m_opened) {
        const int x = horizontalScrollBar()->value() +
                      int(event->position().x() / m_zoom);
        const int y = verticalScrollBar()->value() +
                      int(event->position().y() / m_zoom);
        if (m_mouseDown && (event->buttons() & Qt::LeftButton)) {
            if (!m_dragAnchored) {
                const QPointF delta = event->position() - m_pressPos;
                if (delta.manhattanLength() < 3) {
                    QAbstractScrollArea::mouseMoveEvent(event);
                    return;
                }
                requestSelect(0, m_dragStartX, m_dragStartY);
                m_dragAnchored = true;
            }
            requestSelect(1, x, y);
            m_hasSelection = true;
            return;
        }
        requestHover(x, y);
    } else {
        viewport()->setCursor(Qt::ArrowCursor);
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void ProcView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_mouseDown = false;
        if (m_opened && m_worker) {
            QPointer<ProcView> self(this);
            ProcWorker *worker = m_worker;
            QMetaObject::invokeMethod(worker, [worker, self]() {
                const bool changed = worker->release();
                if (!self || !changed)
                    return;
                QMetaObject::invokeMethod(self.data(), [self]() {
                    if (self)
                        self->requestRender();
                });
            });
        }
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void ProcView::keyPressEvent(QKeyEvent *event) {
    QScrollBar *v = verticalScrollBar();
    QScrollBar *h = horizontalScrollBar();
    if (event->key() == Qt::Key_F12) {
        toggleConsole();
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_C) {
            copySelection();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_A) {
            selectAll();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_F) {
            openFindBar();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_G) {
            runFind(event->modifiers() & Qt::ShiftModifier ? 2 : 1);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_J &&
            (event->modifiers() & Qt::ShiftModifier)) {
            toggleConsole();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_P) {
            savePageAs(true);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_S) {
            savePageAs(false);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Plus ||
            event->key() == Qt::Key_Equal) {
            zoomIn();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Minus) {
            zoomOut();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_0) {
            zoomReset();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape && m_findBar && m_findBar->isVisible()) {
        closeFindBar();
        event->accept();
        return;
    }
    sendKey(0, event);
    switch (event->key()) {
    case Qt::Key_Down:
        v->setValue(v->value() + v->singleStep());
        break;
    case Qt::Key_Up:
        v->setValue(v->value() - v->singleStep());
        break;
    case Qt::Key_Right:
        h->setValue(h->value() + h->singleStep());
        break;
    case Qt::Key_Left:
        h->setValue(h->value() - h->singleStep());
        break;
    case Qt::Key_PageDown:
        v->setValue(v->value() + v->pageStep());
        break;
    case Qt::Key_PageUp:
        v->setValue(v->value() - v->pageStep());
        break;
    case Qt::Key_Space:
        v->setValue(v->value() + (event->modifiers() & Qt::ShiftModifier
                                      ? -v->pageStep()
                                      : v->pageStep()));
        break;
    case Qt::Key_Home:
        v->setValue(0);
        break;
    case Qt::Key_End:
        v->setValue(v->maximum());
        break;
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    event->accept();
}

void ProcView::keyReleaseEvent(QKeyEvent *event) {
    sendKey(1, event);
    QAbstractScrollArea::keyReleaseEvent(event);
}

void ProcView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        const int dy = event->angleDelta().y();
        if (dy > 0)
            zoomIn();
        else if (dy < 0)
            zoomOut();
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void ProcView::setZoom(double zoom) {
    zoom = qBound(double(NS_PROC_ZOOM_MIN), zoom, double(NS_PROC_ZOOM_MAX));
    if (qFuzzyCompare(zoom, m_zoom))
        return;
    m_zoom = zoom;
    updateScrollRanges();
    requestRender();
    emit statusMessage(
        QStringLiteral("Zoom %1%").arg(int(m_zoom * 100.0 + 0.5)));
}

void ProcView::zoomIn() {
    setZoom(m_zoom * NS_PROC_ZOOM_STEP);
}

void ProcView::zoomOut() {
    setZoom(m_zoom / NS_PROC_ZOOM_STEP);
}

void ProcView::zoomReset() {
    setZoom(1.0);
}

QString ProcView::blockingLinkAt(int x, int y) {
    if (!m_opened || !m_worker)
        return QString();
    QString href;
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, x, y, &href]() { href = worker->linkAt(x, y); },
        Qt::BlockingQueuedConnection);
    return href;
}

void ProcView::contextMenuEvent(QContextMenuEvent *event) {
    if (!m_opened) {
        QAbstractScrollArea::contextMenuEvent(event);
        return;
    }
    const int x = horizontalScrollBar()->value() +
                  int(event->pos().x() / m_zoom);
    const int y = verticalScrollBar()->value() +
                  int(event->pos().y() / m_zoom);
    const QString href = blockingLinkAt(x, y);

    QMenu menu(this);
    if (m_hasSelection) {
        menu.addAction(QStringLiteral("Copy"), this,
                       [this]() { copySelection(); });
        menu.addSeparator();
    }
    if (!href.isEmpty()) {
        menu.addAction(QStringLiteral("Open Link"), this,
                       [this, href]() { load(href); });
        menu.addAction(QStringLiteral("Open Link in New Tab"), this,
                       [this, href]() { emit linkRequestedInNewTab(href); });
        menu.addAction(QStringLiteral("Copy Link Address"), this, [href]() {
            QGuiApplication::clipboard()->setText(href);
        });
        menu.addSeparator();
    }
    QAction *back = menu.addAction(QStringLiteral("Back"), this,
                                   &ProcView::back);
    back->setEnabled(canGoBack());
    QAction *fwd = menu.addAction(QStringLiteral("Forward"), this,
                                  &ProcView::forward);
    fwd->setEnabled(canGoForward());
    menu.addAction(QStringLiteral("Reload"), this, &ProcView::reload);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Copy Page Address"), this, [this]() {
        QGuiApplication::clipboard()->setText(m_currentUrl);
    });
    menu.addAction(QStringLiteral("Save Page as PDF…"), this,
                   [this]() { savePageAs(true); });
    menu.addAction(QStringLiteral("Save Page as Image…"), this,
                   [this]() { savePageAs(false); });
    menu.exec(event->globalPos());
}

void ProcView::ensureFindBar() {
    if (m_findBar)
        return;
    m_findBar = new QWidget(viewport());
    m_findBar->setAutoFillBackground(true);
    auto *layout = new QHBoxLayout(m_findBar);
    layout->setContentsMargins(6, 4, 6, 4);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(QStringLiteral("Find in page"));
    m_findEdit->setFixedWidth(220);
    m_findLabel = new QLabel(m_findBar);
    m_findLabel->setMinimumWidth(56);
    auto *prev = new QPushButton(QStringLiteral("▲"), m_findBar);
    auto *next = new QPushButton(QStringLiteral("▼"), m_findBar);
    auto *close = new QPushButton(QStringLiteral("✕"), m_findBar);
    for (QPushButton *b : {prev, next, close})
        b->setFixedWidth(28);
    layout->addWidget(m_findEdit);
    layout->addWidget(m_findLabel);
    layout->addWidget(prev);
    layout->addWidget(next);
    layout->addWidget(close);
    connect(m_findEdit, &QLineEdit::textChanged, this,
            [this]() { runFind(0); });
    connect(m_findEdit, &QLineEdit::returnPressed, this,
            [this]() { runFind(1); });
    connect(prev, &QPushButton::clicked, this, [this]() { runFind(2); });
    connect(next, &QPushButton::clicked, this, [this]() { runFind(1); });
    connect(close, &QPushButton::clicked, this, [this]() { closeFindBar(); });
    m_findBar->adjustSize();
    m_findBar->hide();
}

void ProcView::openFindBar() {
    ensureFindBar();
    m_findBar->move(viewport()->width() - m_findBar->width() - 4, 4);
    m_findBar->show();
    m_findBar->raise();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    if (!m_findEdit->text().isEmpty())
        runFind(0);
}

void ProcView::closeFindBar() {
    if (!m_findBar)
        return;
    m_findBar->hide();
    if (m_worker) {
        ProcWorker *worker = m_worker;
        QMetaObject::invokeMethod(
            worker, [worker]() {
                int t, c, s;
                worker->find(QString(), false, 0, 0, &t, &c, &s);
            }, Qt::QueuedConnection);
    }
    requestRender();
    setFocus();
}

void ProcView::runFind(int direction) {
    if (!m_opened || !m_worker || !m_findEdit)
        return;
    const QString query = m_findEdit->text();
    const int fromY = verticalScrollBar()->value();
    int total = 0, current = 0, scrollY = 0;
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker,
        [worker, query, direction, fromY, &total, &current, &scrollY]() {
            worker->find(query, false, direction, fromY, &total, &current,
                         &scrollY);
        },
        Qt::BlockingQueuedConnection);
    if (m_findLabel) {
        if (total > 0)
            m_findLabel->setText(QStringLiteral("%1/%2").arg(current).arg(total));
        else
            m_findLabel->setText(query.isEmpty() ? QString()
                                                 : QStringLiteral("No results"));
    }
    if (total > 0)
        verticalScrollBar()->setValue(qMax(0, scrollY - 40));
    requestRender();
}

void ProcView::ensureConsole() {
    if (m_console)
        return;
    m_console = new QWidget(this, Qt::Window);
    m_console->setWindowTitle(QStringLiteral("JavaScript Console"));
    m_console->resize(640, 320);
    m_console->installEventFilter(this);
    auto *layout = new QVBoxLayout(m_console);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(m_console);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(6, 2, 6, 2);
    auto *clear = new QPushButton(QStringLiteral("Clear"), header);
    hl->addStretch();
    hl->addWidget(clear);

    m_consoleLog = new QPlainTextEdit(m_console);
    m_consoleLog->setReadOnly(true);
    m_consoleLog->setMaximumBlockCount(5000);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_consoleLog->setFont(mono);
    m_consoleInput = new QLineEdit(m_console);
    m_consoleInput->setPlaceholderText(
        QStringLiteral("Evaluate JavaScript and press Enter"));

    layout->addWidget(header);
    layout->addWidget(m_consoleLog, 1);
    layout->addWidget(m_consoleInput);

    connect(clear, &QPushButton::clicked, this,
            [this]() { m_consoleLog->clear(); });
    connect(m_consoleInput, &QLineEdit::returnPressed, this,
            &ProcView::runEval);

    m_consoleTimer = new QTimer(this);
    m_consoleTimer->setInterval(NS_PROC_CONSOLE_POLL_MS);
    connect(m_consoleTimer, &QTimer::timeout, this, &ProcView::pollConsole);
    m_console->hide();
}

bool ProcView::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_console && event->type() == QEvent::Close) {
        m_consoleOpen = false;
        if (m_consoleTimer)
            m_consoleTimer->stop();
        setFocus();
    }
    return QAbstractScrollArea::eventFilter(watched, event);
}

void ProcView::toggleConsole() {
    ensureConsole();
    m_consoleOpen = !m_consoleOpen;
    if (m_consoleOpen) {
        m_console->show();
        m_console->raise();
        m_console->activateWindow();
        m_consoleTimer->start();
        pollConsole();
        m_consoleInput->setFocus();
    } else {
        m_consoleTimer->stop();
        m_console->hide();
        setFocus();
    }
}

void ProcView::pollConsole() {
    if (!m_consoleOpen || !m_opened || !m_worker || !m_consoleLog)
        return;
    QString out;
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, &out]() { out = worker->consolePoll(); },
        Qt::BlockingQueuedConnection);
    if (!out.isEmpty()) {
        if (out.endsWith(QLatin1Char('\n')))
            out.chop(1);
        m_consoleLog->appendPlainText(out);
    }
}

void ProcView::runEval() {
    if (!m_opened || !m_worker || !m_consoleInput)
        return;
    const QString src = m_consoleInput->text();
    if (src.isEmpty())
        return;
    m_consoleLog->appendPlainText(QStringLiteral("> ") + src);
    QString result;
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, src, &result]() { result = worker->eval(src); },
        Qt::BlockingQueuedConnection);
    m_consoleLog->appendPlainText(result.isEmpty() ? QStringLiteral("undefined")
                                                   : result);
    m_consoleInput->clear();
    requestRender();
}

void ProcView::savePageAs(bool pdf) {
    if (!m_opened || !m_worker)
        return;
    const QString suggested =
        (m_currentTitle.isEmpty() ? QStringLiteral("page") : m_currentTitle) +
        (pdf ? QStringLiteral(".pdf") : QStringLiteral(".png"));
    const QString dest = QFileDialog::getSaveFileName(
        this, pdf ? QStringLiteral("Save page as PDF")
                  : QStringLiteral("Save page as PNG"),
        QDir(QDir::homePath()).filePath(suggested));
    if (dest.isEmpty())
        return;
    QString runtime =
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    QDir().mkpath(runtime);
    const QString tmp = QDir(runtime).filePath(
        QStringLiteral("nordstjernen-export-%1.%2")
            .arg(QCoreApplication::applicationPid())
            .arg(pdf ? QStringLiteral("pdf") : QStringLiteral("png")));
    bool ok = false;
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, tmp, &ok]() { ok = worker->exportPage(tmp); },
        Qt::BlockingQueuedConnection);
    if (ok) {
        QFile::remove(dest);
        ok = QFile::copy(tmp, dest);
    }
    QFile::remove(tmp);
    emit statusMessage(ok ? QStringLiteral("Saved %1").arg(dest)
                          : QStringLiteral("Could not save page"));
}

bool ProcView::maybeLaunchMedia(int x, int y) {
    if (!m_opened || !m_worker)
        return false;
    QString url;
    int isVideo = 0, stream = 0;
    ProcWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, x, y, &url, &isVideo, &stream]() {
            url = worker->mediaAt(x, y, &isVideo, &stream);
        },
        Qt::BlockingQueuedConnection);
    if (url.isEmpty())
        return false;
    QByteArray bytes = url.toUtf8();
    char *suggestApp = nullptr;
    char *suggestUrl = nullptr;
    ns_media_status st = ns_media_try_launch(bytes.constData(), stream != 0,
                                             &suggestApp, &suggestUrl);
    QString message;
    if (st == NS_MEDIA_LAUNCHED) {
        message = QStringLiteral("Opening %1 in external player...")
                      .arg(isVideo ? QStringLiteral("video")
                                   : QStringLiteral("audio"));
    } else if (st == NS_MEDIA_UNSAFE) {
        message = QStringLiteral("Blocked unsafe media URL");
    } else if (st == NS_MEDIA_NEED_YTDLP) {
        message = QStringLiteral("Install yt-dlp to open this media stream");
    } else if (st == NS_MEDIA_NO_PLAYER) {
        message = suggestApp
                      ? QStringLiteral("Install %1 to open this media")
                            .arg(QString::fromUtf8(suggestApp))
                      : QStringLiteral("No application registered to open this media");
    } else {
        message = QStringLiteral("Could not open media");
    }
    if (suggestUrl && st != NS_MEDIA_LAUNCHED)
        message += QStringLiteral(" (%1)").arg(QString::fromUtf8(suggestUrl));
    g_free(suggestApp);
    g_free(suggestUrl);
    emit statusMessage(message);
    return st == NS_MEDIA_LAUNCHED;
}
