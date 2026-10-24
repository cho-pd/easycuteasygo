#include "engine/AudioFileWriter.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/GpuDevice.h"
#include "engine/GpuPreference.h"
#include "engine/HwAccel.h"
#include "engine/ReverseProxyCache.h"
#ifndef Q_OS_ANDROID
#include "HeadlessApp.h"
#include "mcp/McpStdio.h"
#endif
#include "models/AddonManager.h"
#include "models/AppController.h"
#include "models/AssetLibrary.h"
#include "models/EditorState.h"
#include "models/FileDialogs.h"
#include "models/Haptics.h"
#include "models/LayoutStore.h"
#include "models/MarketClient.h"
#include "models/UpdateChecker.h"
#include "engine/VaapiZeroCopy.h"
#include "ClipPreviewImageProvider.h"
#include "DriftImageProvider.h"
#include "MulticamImageProvider.h"
#include "SegmentImageProvider.h"
#include "ShapePreviewImageProvider.h"
#include "TextStylePreviewImageProvider.h"
#include "preview/PreviewItem.h"

// QApplication (not QGuiApplication) is required so QFileDialog can use the
// native platform file picker, which routes through xdg-desktop-portal.
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFileOpenEvent>
#include <QIcon>
#include <QImageReader>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QStandardPaths>
#include <QStringList>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QtQml/qqml.h>
#include <QFile>
#include <QUrl>
#include <QCryptographicHash> // VEDIT: single instance
#include <QQmlContext>
#include <QElapsedTimer>
#include <QScreen>
#include <QRasterWindow>
#include <QTimer>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath> // VEDIT: the scissors
#include <atomic>
#include <cmath>
#include <thread>
#include <cstdio>
#if __has_include("vedit_build_number.h")
#include "vedit_build_number.h" // VEDIT: written by the build script
#endif
#ifndef VEDIT_BUILD_NUMBER
#define VEDIT_BUILD_NUMBER 0
#endif
#include <QLocalServer>
#include <QLocalSocket>
#include <QWindow>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef Q_OS_ANDROID
#include "core/Project.h"
#include "engine/FrameCompositor.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#endif

extern "C" {
#include <libavutil/log.h>
}

#ifdef Q_OS_WIN
// Exports NVIDIA Optimus and AMD PowerXpress look up in the executable: 1 asks for the discrete
// GPU. Variables rather than constants so main() can set them from the stored preference before
// anything loads a graphics driver — Qt loads OpenGL lazily, at the first context. Zero leaves
// the choice to the driver's own profile, which is also what an absent export means.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0;
}
#endif

// VEDIT (조박사님): one window at a time. A second launch hands its arguments (a project file
// double-clicked in Explorer) to the copy already running, which brings its window to the front,
// and then quits. Keyed by user and install folder, so a developer build and the installed app
// don't block each other. VEDIT_ALLOW_MULTIPLE=1 turns it off.
static QString veditInstanceKey()
{
    const QByteArray id = (QCoreApplication::applicationDirPath().toLower() + QLatin1Char('|')
                           + qEnvironmentVariable("USERNAME"))
                              .toUtf8();
    return QStringLiteral("EasyCutEasyGo-")
           + QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(16));
}

static void veditBringToFront()
{
    for (QWindow *w : QGuiApplication::topLevelWindows()) {
        if (!w->isVisible() || w->transientParent() || w->type() != Qt::Window)
            continue;
        if (w->windowStates() & Qt::WindowMinimized)
            w->setWindowStates(w->windowStates() & ~Qt::WindowMinimized);
        w->raise();
        w->requestActivate();
#ifdef Q_OS_WIN
        SetForegroundWindow(reinterpret_cast<HWND>(w->winId()));
#endif
    }
}

namespace {

bool verboseLoggingRequested(int argc, char *argv[])
{
    if (qEnvironmentVariableIntValue("DRIFT_VERBOSE") != 0)
        return true;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--verbose") == 0)
            return true;
    }
    return false;
}

// Qt only writes messages to stderr when it thinks a console is attached — a
// controlling TTY on Unix, a console window on Windows. Everywhere else it hands
// them to the platform's own sink instead: journald on Linux, OutputDebugString on
// Windows. So `drift > log.txt 2>&1`, which is exactly what someone does to collect
// a log for a bug report, produced an empty file and every qWarning went to the
// journal unnoticed. Send them to stderr always.
//
// Not on Android: there the platform sink is logcat, which *is* the way to read an
// Android app's log, and a GUI app's stderr goes to /dev/null. Forcing it there
// would throw the logs away rather than redirect them.
//
// QT_LOGGING_TO_CONSOLE would also work but is deprecated in Qt 6 and prints a
// warning about itself on every launch. Anything the user set already wins.
void forceStderrLogging()
{
#ifndef Q_OS_ANDROID
    if (qEnvironmentVariableIsEmpty("QT_FORCE_STDERR_LOGGING")
        && qEnvironmentVariableIsEmpty("QT_ASSUME_STDERR_HAS_CONSOLE")
        && qEnvironmentVariableIsEmpty("QT_LOGGING_TO_CONSOLE")) {
        qputenv("QT_FORCE_STDERR_LOGGING", "1");
    }
#endif
}

// FFmpeg logs at INFO and Qt prints every qDebug/qInfo, which buries the failures worth acting on
// under per-frame filtergraph chatter. qWarning is this codebase's failure channel, so it stays on
// either way. QT_LOGGING_RULES is applied after these (EnvironmentRules outrank ApiRules), so it
// still overrides them.
void applyLogLevel(bool verbose)
{
    QLoggingCategory::setFilterRules(verbose
                                         ? QStringLiteral("*.debug=true\n"
                                                          "*.info=true\n"
                                                          "qt.*.debug=false")
                                         : QStringLiteral("*.debug=false\n"
                                                          "*.info=false"));
    av_log_set_level(verbose ? AV_LOG_VERBOSE : AV_LOG_ERROR);
}

// QML's Image loads through the engine's own QNetworkAccessManager, not through any that a
// model owns, and by default that one has no cache at all — so a server's Cache-Control was
// ignored and every remote image was refetched whenever its decoded pixmap fell out of Qt's
// in-memory cache. Scrolling the marketplace grid re-downloaded thumbnails it had already
// fetched, and a restart refetched all of them.
//
// create() is documented as callable from more than one thread, so it must not hand out
// shared state; each manager gets its own cache object over the same directory, which is how
// QNetworkDiskCache is meant to be used.
class CachedNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *manager = new QNetworkAccessManager(parent);
        auto *cache = new QNetworkDiskCache(manager);
        cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                 + QStringLiteral("/qml-http"));
        cache->setMaximumCacheSize(256LL * 1024 * 1024);
        manager->setCache(cache);
        return manager;
    }
};

class FileOpenFilter : public QObject
{
public:
    explicit FileOpenFilter(AppController *controller, MarketClient *market, QObject *parent = nullptr)
        : QObject(parent)
        , m_controller(controller)
        , m_market(market)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const auto *open = static_cast<QFileOpenEvent *>(event);
            if (m_market && m_market->handleIncomingUrl(open->url()))
                return true;
            m_controller->queueExternalProject(open->url());
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    AppController *m_controller = nullptr;
    MarketClient *m_market = nullptr;
};

#ifdef Q_OS_ANDROID
// On-device render check. With <AppDataLocation>/selftest.json in place, composite one frame and
// write selftest.png beside it instead of starting the UI. This is tools/renderframe moved onto
// the device: it exercises FFmpeg decode, the GLES offscreen context, the shader translation and
// package discovery with no QML, no preview item and no clock in the way.
bool runSelfTest()
{
    QString dir;
    QString projectPath;
    const QStringList candidates =
        QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (const QString &candidate : candidates) {
        const QString path = QDir(candidate).filePath(QStringLiteral("selftest.json"));
        if (QFile::exists(path)) {
            dir = candidate;
            projectPath = path;
            break;
        }
    }

    if (projectPath.isEmpty()) {
        qWarning("selftest: no selftest.json in any of: %s",
                 qPrintable(candidates.join(QLatin1String(", "))));
        return false;
    }

    qWarning("selftest: loading %s", qPrintable(projectPath));

    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("selftest: cannot open %s", qPrintable(projectPath));
        return true;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning("selftest: not a JSON object");
        return true;
    }

    QString error;
    drift::Project project = drift::Project::fromJson(doc.object(), &error);
    if (!error.isEmpty()) {
        qWarning("selftest: project load failed: %s", qPrintable(error));
        return true;
    }

    const drift::TimeUs timeUs = qEnvironmentVariableIntValue("DRIFT_SELFTEST_TIME_US");

    FrameCompositor compositor;
    compositor.setProject(&project);
    const QImage frame = compositor.compositeAt(timeUs);
    if (frame.isNull()) {
        qWarning("selftest: compositor returned an empty frame at %lld us",
                 static_cast<long long>(timeUs));
        return true;
    }

    const QString outPath = QDir(dir).filePath(QStringLiteral("selftest.png"));
    if (!frame.save(outPath))
        qWarning("selftest: failed to write %s", qPrintable(outPath));
    else
        qWarning("selftest: wrote %s (%dx%d)", qPrintable(outPath), frame.width(), frame.height());

    return true;
}
#endif // Q_OS_ANDROID

#ifndef Q_OS_ANDROID
// Creates a throwaway context in `format`. On success reports what the driver
// actually handed back, which is not always what was asked for.
bool probeOpenGl(const QSurfaceFormat &format, QSurfaceFormat *obtained = nullptr,
                 QString *renderer = nullptr, QString *vendor = nullptr)
{
    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    if (!surface.isValid())
        return false;

    QOpenGLContext ctx;
    ctx.setFormat(format);
    if (!ctx.create())
        return false;
    if (obtained)
        *obtained = ctx.format();
    if ((renderer || vendor) && ctx.makeCurrent(&surface)) {
        if (QOpenGLFunctions *fn = ctx.functions()) {
            if (renderer) {
                if (const char *name = reinterpret_cast<const char *>(fn->glGetString(GL_RENDERER)))
                    *renderer = QString::fromUtf8(name);
            }
            if (vendor) {
                if (const char *name = reinterpret_cast<const char *>(fn->glGetString(GL_VENDOR)))
                    *vendor = QString::fromUtf8(name);
            }
        }
        // While a context is current: on EGL this is what names the DRM device Qt draws
        // through, which is finer-grained than the vendor string and the only way to tell two
        // GPUs of the same vendor apart.
        drift::gpu::probeRenderDrmNode();
        ctx.doneCurrent();
    }
    return true;
}

// Qt Quick asks for QSurfaceFormat::defaultFormat() and calls qFatal() when it
// cannot have it, so on a driver below OpenGL 3.3 Drift aborts before there is a
// window to put an error in — the app simply vanishes. Say why first.
void warnIfNoOpenGl()
{
    // Probe exactly what Qt Quick will ask for — and check what came *back*, not
    // just that creation succeeded. Both WGL and GLX quietly clamp the request to
    // whatever the driver can do rather than failing it, so a machine that tops out
    // at 3.0 hands back a valid 3.0 context and only the version reveals it. That
    // clamp is the whole of issue #139: Qt Quick is content, and the compositor's
    // 3.3 floor is what breaks.
    auto atLeast33 = [](const QSurfaceFormat &f) {
        return f.majorVersion() > 3 || (f.majorVersion() == 3 && f.minorVersion() >= 3);
    };

    QSurfaceFormat obtained;
    QString renderer;
    QString vendor;
    bool haveGl = probeOpenGl(QSurfaceFormat::defaultFormat(), &obtained, &renderer, &vendor);
    // Tell the decoder which GPU will be drawing, while this is the only context that has
    // existed. Decoding on a card that is not the one compositing costs a PCIe round trip per
    // previewed frame, and a reader keeps whichever backend it opened with, so this has to be
    // known before the first clip does — not whenever the compositor happens to come up.
    drift::hwaccel::setRenderVendor(vendor);
    if (haveGl && atLeast33(obtained))
        return;

    // The 3.3 request did not give us 3.3. The two platforms disagree about how it
    // fails — WGL clamps and hands back a valid older context, GLX refuses outright
    // — so ask again for the bare minimum. That is what reveals the driver's real
    // ceiling, and it is the difference between "your driver is too old" and "you
    // have no driver", which are very different things to tell someone.
    if (!haveGl)
        haveGl = probeOpenGl(QSurfaceFormat(), &obtained, &renderer, &vendor);
    if (!vendor.isEmpty())
        drift::hwaccel::setRenderVendor(vendor);

    const QString driver =
        renderer.isEmpty() ? QCoreApplication::translate("main", "unknown") : renderer;

    QString title;
    QString body;
    if (!haveGl) {
        title = QCoreApplication::translate("main", "No OpenGL driver");
        body = QCoreApplication::translate(
            "main",
            "Drift could not create an OpenGL context, so it cannot draw its interface "
            "or render the preview.\n\nInstall or update your graphics driver.");
    } else if (atLeast33(obtained)) {
        // New enough, so it is the 3.3 *core profile* that could not be had — a
        // driver or session quirk, not old hardware. Do not tell someone their GPU
        // is too old when it is not.
        title = QCoreApplication::translate("main", "OpenGL context unavailable");
        body = QCoreApplication::translate(
                   "main",
                   "Drift could not create an OpenGL 3.3 core profile context, though "
                   "this driver reports OpenGL %1.%2 (%3).\n\nThe video preview cannot "
                   "render. Updating your graphics driver may help.")
                   .arg(obtained.majorVersion())
                   .arg(obtained.minorVersion())
                   .arg(driver);
    } else {
        title = QCoreApplication::translate("main", "Graphics driver is too old");
        // Deliberately does not promise what happens next: below 3.3 the preview
        // cannot render on any platform, and on some Drift cannot start at all.
        body = QCoreApplication::translate(
                   "main",
                   "Drift needs OpenGL 3.3, but this graphics driver only provides "
                   "OpenGL %1.%2 (%3).\n\nThe video preview cannot render, and Drift may "
                   "not start at all. Update your graphics driver, or run Drift on a "
                   "machine with a newer GPU.")
                   .arg(obtained.majorVersion())
                   .arg(obtained.minorVersion())
                   .arg(driver);
    }

    // Also to the log: the dialog cannot be read from a terminal or a bug report.
    qCritical("%s: %s", qUtf8Printable(title), qUtf8Printable(body));
    QMessageBox::critical(nullptr, title, body);
}
#endif

} // namespace

// VEDIT: the start-up splash. A bare QRasterWindow showing a picture: a QSplashScreen is a widget,
// and the first widget shown loads the style and the whole font database — a second before it
// appeared at all.
class VeditSplash : public QRasterWindow
{
public:
    explicit VeditSplash(const QPixmap &picture) : m_picture(picture)
    {
        setFlags(Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        const QSize size = (QSizeF(picture.size()) / picture.devicePixelRatio()).toSize();
        resize(size);
        if (QScreen *screen = QGuiApplication::primaryScreen())
            setPosition(screen->availableGeometry().center() - QPoint(size.width() / 2, size.height() / 2));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawPixmap(QRect(QPoint(0, 0), size()), m_picture);
    }

private:
    QPixmap m_picture;
};

#ifdef Q_OS_WIN
// VEDIT (조박사님): in place of "불러오는 중…", a pair of scissors snips its way along a dotted line
// under the title, like a cartoon. Startup blocks the GUI thread for seconds (fonts, the whole
// QML scene), so a Qt window could not move: this one is a plain Win32 window owned by a thread
// of its own, drawn with QPainter into a QImage (safe off the GUI thread) and blitted.
class VeditCutSplash
{
public:
    explicit VeditCutSplash(const QImage &picture) : m_picture(picture) {}
    ~VeditCutSplash() { close(); }

    void show()
    {
        m_clock.start();
        m_thread = std::thread([this] { run(); });
    }

    void close()
    {
        m_closing.store(true);
        if (HWND hwnd = m_hwnd.load())
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto *self = reinterpret_cast<VeditCutSplash *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
        case WM_NCCREATE: {
            auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            break;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT:
            if (self) {
                self->renderFrame();
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(hwnd, &ps);
                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = self->m_frame.width();
                bmi.bmiHeader.biHeight = -self->m_frame.height(); // top-down
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                SetDIBitsToDevice(dc, 0, 0, self->m_frame.width(), self->m_frame.height(), 0, 0, 0,
                                  self->m_frame.height(), self->m_frame.constBits(), &bmi, DIB_RGB_COLORS);
                EndPaint(hwnd, &ps);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void run()
    {
        const double scale = qMax(1.0, GetDpiForSystem() / 96.0);
        const int w = qRound(560 * scale);
        const int h = qRound(300 * scale);
        m_base = m_picture.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                     .convertToFormat(QImage::Format_RGB32);
        m_frame = QImage(w, h, QImage::Format_RGB32);

        WNDCLASSW wc = {};
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"VeditCutSplash";
        wc.hCursor = LoadCursorW(nullptr, IDC_APPSTARTING);
        RegisterClassW(&wc);
        RECT work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int x = work.left + (work.right - work.left - w) / 2;
        const int y = work.top + (work.bottom - work.top - h) / 2;
        HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Easy Cut, Easy Go!",
                                    WS_POPUP, x, y, w, h, nullptr, nullptr, wc.hInstance, this);
        if (!hwnd)
            return;
        m_hwnd.store(hwnd);
        if (m_closing.load()) {
            DestroyWindow(hwnd);
        } else {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            SetTimer(hwnd, 1, 30, nullptr);
        }
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        m_hwnd.store(nullptr);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
    }

    // One frame, drawn in the picture's own 1120 x 600 space.
    void renderFrame()
    {
        const double t = m_clock.elapsed() / 1000.0;
        QPainter p(&m_frame);
        p.drawImage(0, 0, m_base);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(m_frame.width() / 1120.0, m_frame.height() / 600.0);

        const double x0 = 322, x1 = 892, lineY = 432;
        const double period = 2.8; // one trip along the line
        const double phase = std::fmod(t, period) / period;
        const double headX = x0 + (x1 - x0) * phase;

        // ahead of the scissors a solid line still to cut; behind them it is cut — dotted
        p.setPen(QPen(QColor(110, 118, 135), 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(headX, lineY), QPointF(x1, lineY));
        QPen dots(QColor(255, 194, 26), 2.4, Qt::CustomDashLine, Qt::RoundCap);
        dots.setDashPattern({2.5, 3.5});
        p.setPen(dots);
        p.drawLine(QPointF(x0, lineY), QPointF(qMax(x0, headX - 8), lineY));

        // the scissors: snip, snip — the blades open and close three times a second
        const double snip = 0.5 + 0.5 * std::sin(t * 2.0 * 3.14159265358979 * 3.0);
        const double angle = 4.0 + 22.0 * snip; // degrees each side
        p.save();
        p.translate(headX, lineY);
        p.translate(0, -1.5 * std::sin(t * 2.0 * 3.14159265358979 * 3.0)); // a little bob as it goes
        auto blade = [&p](double deg, bool upper) {
            p.save();
            p.rotate(upper ? -deg : deg);
            const double s = upper ? -1.0 : 1.0;
            QPainterPath path;
            path.moveTo(-2, 0);
            path.quadTo(26, s * 9, 56, s * 1.2);
            path.lineTo(56, 0);
            path.quadTo(26, -s * 1.5, -2, -s * 2.5);
            path.closeSubpath();
            p.setPen(QPen(QColor(70, 78, 92), 1.2));
            p.setBrush(QColor(222, 228, 238));
            p.drawPath(path);
            // the handle: an arm back from the pivot and a ring
            p.setPen(QPen(QColor(255, 194, 26), 5.0, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(-2, -s * 1.0), QPointF(-16, -s * 9.0));
            p.drawEllipse(QPointF(-27, -s * 14.0), 10.5, 8.0);
            p.restore();
        };
        blade(angle, true);
        blade(angle, false);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 66, 80));
        p.drawEllipse(QPointF(0, 0), 3.2, 3.2);
        p.restore();
    }

    QImage m_picture;
    QImage m_base;
    QImage m_frame;
    std::thread m_thread;
    std::atomic<HWND> m_hwnd{nullptr};
    std::atomic<bool> m_closing{false};
    QElapsedTimer m_clock;
};
#endif

// VEDIT: startup timing, printed to stderr when VEDIT_STARTUP_TRACE is set.
static QElapsedTimer &veditStartupClock()
{
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    return clock;
}
static void veditTrace(const char *label)
{
    static const bool on = qEnvironmentVariableIsSet("VEDIT_STARTUP_TRACE");
    if (on)
        fprintf(stderr, "[startup] %6lld ms  %s\n", (long long)veditStartupClock().elapsed(), label);
}

int main(int argc, char *argv[])
{
    veditStartupClock(); // VEDIT
    forceStderrLogging();
    applyLogLevel(verboseLoggingRequested(argc, argv));

#ifndef Q_OS_ANDROID
    // --headless is checked first: it also accepts --mcp-stdio, as the transport to serve
    // rather than as a request to attach to an editor running elsewhere.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--headless") == 0)
            return drift::runHeadless(argc, argv);
    }
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--mcp-stdio") == 0) {
            QCoreApplication app(argc, argv);
            QCoreApplication::setApplicationName("EasyCutEasyGo") /* VEDIT */;
            QCoreApplication::setOrganizationName("EasyCutEasyGo") /* VEDIT */;
            return drift::mcp::runStdioAttach();
        }
    }
#endif

#ifdef Q_OS_ANDROID
    // Android is GLES-only; the desktop 3.3 core profile the engine asks for cannot be created
    // here at all. Both contexts that matter — the Qt Quick scene graph's and the compositor's
    // offscreen one in GlRuntime — must agree on the version before they can share textures.
    QSurfaceFormat androidFormat;
    androidFormat.setRenderableType(QSurfaceFormat::OpenGLES);
    androidFormat.setVersion(3, 0);
    androidFormat.setDepthBufferSize(0);
    androidFormat.setStencilBufferSize(0);
    QSurfaceFormat::setDefaultFormat(androidFormat);
#else
    // On NVIDIA/Wayland, leaving the API unspecified makes EGL interpret 3.3
    // as an invalid GLES version and fail with EGL_BAD_MATCH. Start from the
    // default format to retain the platform-selected window-buffer attributes.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
#endif

    // Keep Qt Quick on OpenGL and create its global share context before the
    // application, enabling zero-copy texture handoff from the compositor.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

#ifdef Q_OS_WIN
    // Before allowing desktop OpenGL, Qt looks the GPU up in a blacklist keyed on
    // the vendor and device id it gets from Direct3D 9. When that probe fails the
    // ids come back 0x0000, which matches the list's "Standard VGA" entry, and Qt
    // silently loads opengl32sw.dll instead — Mesa llvmpipe, OpenGL 3.0 at best.
    // Qt Quick runs on 3.0, so the window looks fine while the compositor's 3.3
    // floor fails and the preview stays black on cards that would have run it.
    // The list also still bans a set of pre-2015 Intel parts outright. Skip the id
    // lookup and let context creation decide: Qt's own testDesktopGL() still runs,
    // so a machine that genuinely cannot do desktop GL still falls back.
    // Anything the user set — including a custom buglist — still wins.
    if (qEnvironmentVariableIsEmpty("QT_NO_OPENGL_BUGLIST")
        && qEnvironmentVariableIsEmpty("QT_OPENGL")
        && qEnvironmentVariableIsEmpty("QT_OPENGL_BUGLIST")) {
        qputenv("QT_NO_OPENGL_BUGLIST", "1");
    }
#endif

    // Names must be set before reading QSettings for ui/scale, and QT_SCALE_FACTOR
    // must be in the environment before QApplication is constructed.
    QCoreApplication::setApplicationName("EasyCutEasyGo") /* VEDIT */;
    QCoreApplication::setOrganizationName("EasyCutEasyGo") /* VEDIT */;
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Easy Cut, Easy Go!")); // VEDIT
    AppController::applyStoredUiScale();
    // Qt's xcb plugin defaults to GLX, so eglGetCurrentDisplay() is null and
    // zero-copy sticky-disables. Only force EGL when the user opted in — default
    // X11 behaviour stays byte-identical. An explicit QT_XCB_GL_INTEGRATION still wins.
    drift::applyVaapiZeroCopyXcbEgl();

#ifdef Q_OS_WIN
    // Before QApplication, while no graphics driver is loaded and the GPU is still unchosen. The
    // registry preference and the exports each cover drivers that ignore the other.
    if (drift::gpu::applyStoredPreference() == drift::gpu::Preference::HighPerformance) {
        NvOptimusEnablement = 1;
        AmdPowerXpressRequestHighPerformance = 1;
    }
#endif

    QApplication app(argc, argv);
    // VEDIT: the build's launch check (C:/dev/round.ps1 sets VEDIT_SMOKE) keeps out of the real
    // app data — it used to load, rewrite and race the proxy index of the copy in use.
    if (qEnvironmentVariableIsSet("VEDIT_SMOKE"))
        QStandardPaths::setTestModeEnabled(true);
    veditTrace("app");
    veditTrace("qapplication");
    // VEDIT: single instance (see veditInstanceKey).
    QLocalServer *instanceServer = nullptr;
    if (!qEnvironmentVariableIsSet("VEDIT_ALLOW_MULTIPLE")) {
        const QString key = veditInstanceKey();
        QLocalSocket probe;
        probe.connectToServer(key);
        // VEDIT: a running copy answers at once; waiting long only delays every normal start.
        if (probe.state() == QLocalSocket::ConnectedState || probe.waitForConnected(120)) {
#ifdef Q_OS_WIN
            // This process was just started by the user, so it may hand the foreground on.
            AllowSetForegroundWindow(ASFW_ANY);
#endif
            probe.write(app.arguments().mid(1).join(QLatin1Char(0x0A)).toUtf8());
            probe.flush();
            probe.waitForBytesWritten(1000);
            probe.disconnectFromServer();
            return 0;
        }
        QLocalServer::removeServer(key); // left behind by a crash
        instanceServer = new QLocalServer(&app);
        instanceServer->setSocketOptions(QLocalServer::UserAccessOption);
        if (!instanceServer->listen(key)) {
            delete instanceServer;
            instanceServer = nullptr;
        }
    }
    // VEDIT (조박사님: "더블 클릭해도 아무 반응이 없다"): something on screen at once — the rest of
    // startup (fonts, catalogs, the whole QML scene) takes seconds. Closed on the first real frame.
    VeditSplash *splash = nullptr;
#ifdef Q_OS_WIN
    // VEDIT: the moving one (scissors) — its own thread, so it keeps snipping while QML loads.
    static VeditCutSplash *cutSplash = nullptr;
    {
        // Started minimised (the build's smoke test): no splash over someone's work.
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        GetStartupInfoW(&si);
        const bool minimised = (si.dwFlags & STARTF_USESHOWWINDOW)
                               && (si.wShowWindow == SW_SHOWMINIMIZED || si.wShowWindow == SW_MINIMIZE
                                   || si.wShowWindow == SW_SHOWMINNOACTIVE);
        const QImage picture(QStringLiteral(":/app/brand/splash.png"));
        if (!picture.isNull() && !minimised) {
            cutSplash = new VeditCutSplash(picture);
            cutSplash->show();
        }
    }
#else
    {
        QPixmap picture(QStringLiteral(":/app/brand/splash.png"));
        if (!picture.isNull()) {
            picture.setDevicePixelRatio(2.0);
            splash = new VeditSplash(picture);
            splash->show();
            app.processEvents();
        }
    }
#endif
    veditTrace("splash");
    // A missing image plugin is silent everywhere else: the reader just returns a null QImage,
    // so the bin card is blank and the clip renders as nothing with no hint why. On a released
    // APK this line is the only way to tell that from a corrupt file, over adb logcat.
    {
        const QList<QByteArray> formats = QImageReader::supportedImageFormats();
        if (!formats.contains("svg")) {
            qWarning("SVG icons will not display: Qt's SVG image plugin is missing or built "
                     "for a different Qt version than this binary. Install a matching qt6-svg "
                     "(same version as qt6-base).");
        }
        QStringList missing;
        for (const char *format : {"webp", "tiff"}) {
            if (!formats.contains(QByteArray(format)))
                missing.append(QString::fromLatin1(format));
        }
        if (!missing.isEmpty()) {
            qWarning("Qt ImageFormats plugins missing (%s): those stills will not decode. "
                     "Install qt6-imageformats, or add qtimageformats to the Qt kit this was "
                     "built against.",
                     qPrintable(missing.join(QStringLiteral(", "))));
        }
    }
    // Associates the window with the installed .desktop entry so shells (notably
    // Wayland) can find its icon and app metadata.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.cutwire.Drift"));
    // Title bar / taskbar icon when no desktop entry is available (Windows, and
    // Linux runs from the build tree). The .exe still needs the Windows .rc icon
    // for Explorer and pinned-taskbar identity.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/drift.png")));

    // qsTr/tr resolve when the QML engine loads, so translators must be installed first.
    // Protocol strings under src/mcp/ are excluded from the catalog; they stay English.
    AppController::installUiTranslators();

#ifndef Q_OS_ANDROID
    // The whole UI is a Qt Quick scene graph on OpenGL, so with no OpenGL at all
    // there is no window to put an error in — the app would just appear not to
    // start. That is reachable on Windows, where the packages no longer carry Qt's
    // software rasterizer as a fallback. Deliberately narrow: this asks for no
    // particular version, so it fires only when a context cannot be created at
    // all. A driver that is merely too old still starts, and the preview explains
    // itself in the panel instead.
    warnIfNoOpenGl();
    veditTrace("opengl-check");
#endif

    // Registering the bundled fonts needs a QGuiApplication, and must happen before the compositor
    // thread starts touching QFontDatabase.
    reloadFontCatalog();
    veditTrace("fonts");
    // VEDIT: not here — it took 1–2 s of every start. The catalog loads itself on first use
    // (EmojiCatalog's ensureLoadedLocked), i.e. when stickers or an emoji are first needed.
    veditTrace("emoji");

    // Noise-removal A/B snippets are scratch. Anything still here is from a previous session that
    // did not get to clean up after itself.
    drift::sweepDenoisePreviews();
    veditTrace("denoise-sweep");

#ifdef Q_OS_ANDROID
    // Every content:// write is staged through <cache>/staged and unlinked once the copy into the
    // document finishes, so a file still here is debris from an encode that was killed.
    {
        QDir staged(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                    + QStringLiteral("/staged"));
        const QFileInfoList leftovers = staged.entryInfoList(QDir::Files);
        for (const QFileInfo &file : leftovers)
            QFile::remove(file.absoluteFilePath());
    }

    qWarning("app data locations: %s",
             qPrintable(QStandardPaths::standardLocations(QStandardPaths::AppDataLocation)
                            .join(QLatin1String(", "))));
    if (runSelfTest())
        return 0;
#endif

    // Reversed proxies are a pure cache: dropping one only costs the clip its smooth playback, so
    // they are pruned to a budget rather than kept forever the way mattes are.
    drift::ReverseProxyCache::instance().load();
    drift::ReverseProxyCache::instance().sweep(drift::ReverseProxyCache::kDefaultMaxBytes);
    veditTrace("reverse-proxy");

    qmlRegisterType<PreviewItem>("Drift", 1, 0, "PreviewItem");

    static AssetLibrary assetLibrary;
    static EditorState editorState(&assetLibrary);
    veditTrace("editor-state");
    static FileDialogs fileDialogs;
    static AddonManager addonManager;
    static MarketClient marketClient;
    static UpdateChecker updateChecker;
    veditTrace("statics");
    static LayoutStore layoutStore;
    static drift::Haptics haptics;
    editorState.setAddonManager(&addonManager);
    marketClient.setAssetLibrary(&assetLibrary);
    editorState.setMarketClient(&marketClient);
    qmlRegisterSingletonInstance("Drift", 1, 0, "AssetLibrary", &assetLibrary);
    qmlRegisterSingletonInstance("Drift", 1, 0, "BinFolderModel", editorState.binFolderModel());
    qmlRegisterSingletonInstance("Drift", 1, 0, "EditorState", &editorState);
    qmlRegisterSingletonInstance("Drift", 1, 0, "AppController", &editorState);
    qmlRegisterSingletonInstance("Drift", 1, 0, "FileDialogs", &fileDialogs);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Addons", &addonManager);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Market", &marketClient);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Updates", &updateChecker);
    qmlRegisterSingletonInstance("Drift", 1, 0, "LayoutMemory", &layoutStore);
    qmlRegisterSingletonInstance("Drift", 1, 0, "Haptics", &haptics);

    app.installEventFilter(new FileOpenFilter(&editorState, &marketClient, &app));
    {
        // Hoisted deliberately: QCoreApplication::arguments() rebuilds and returns a QStringList
        // BY VALUE on every call, so `const QString &arg = app.arguments().at(i)` bound a
        // reference into a temporary that died at the end of the statement. Appending it then
        // read freed memory — heap corruption that only surfaced when something else happened to
        // reuse the block, which made it look intermittent and unrelated to this loop.
        const QStringList args = app.arguments();
        QStringList forwarded = {args.constFirst()};
        for (int i = 1; i < args.size(); ++i) {
            const QString &arg = args.at(i);
            const QUrl url(arg);
            if (!url.scheme().isEmpty() && marketClient.handleIncomingUrl(url))
                continue;
            forwarded.append(arg);
        }
        editorState.queueExternalProject(AppController::startupProjectUrlFromArguments(forwarded));
    }

    QQmlApplicationEngine engine;
    veditTrace("qml-engine");
    static CachedNetworkAccessManagerFactory networkFactory;
    engine.setNetworkAccessManagerFactory(&networkFactory);
    QObject::connect(&editorState, &AppController::uiLanguageChanged,
                     &engine, &QQmlEngine::retranslate);
    engine.addImageProvider(QStringLiteral("drift"), new DriftImageProvider());
    engine.addImageProvider(QStringLiteral("segment"), new SegmentImageProvider());
    engine.addImageProvider(QStringLiteral("clippreview"), new ClipPreviewImageProvider());
    engine.addImageProvider(QStringLiteral("multicam"), new MulticamImageProvider());
    engine.addImageProvider(QStringLiteral("textstyle"), new TextStylePreviewImageProvider());
    engine.addImageProvider(QStringLiteral("shape"), new ShapePreviewImageProvider());
    engine.addImageProvider(QStringLiteral("textanim"), new TextAnimPreviewImageProvider());
    engine.addImageProvider(QStringLiteral("textlook"), new TextLookPreviewImageProvider());
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QGuiApplication::exit(-1); }, Qt::QueuedConnection);
    // Shell.qml owns the choice between Main.qml (desktop) and AndroidMain.qml (touch) and can
    // re-make it at runtime as the window crosses the compact breakpoint. Resolved here in one
    // place, in a fixed precedence: explicit argument, then environment, then platform default.
    const QStringList shellArgs = app.arguments();
    QString shellPreference = QStringLiteral("auto");
    if (shellArgs.contains(QStringLiteral("--shell=mobile")))
        shellPreference = QStringLiteral("mobile");
    else if (shellArgs.contains(QStringLiteral("--shell=desktop")))
        shellPreference = QStringLiteral("desktop");
    else if (qEnvironmentVariableIsSet("DRIFT_SHELL"))
        shellPreference = qEnvironmentVariable("DRIFT_SHELL");
    if (shellPreference != QLatin1String("mobile") && shellPreference != QLatin1String("desktop"))
        shellPreference = QStringLiteral("auto");

    engine.setInitialProperties({{QStringLiteral("shellPreference"), shellPreference}});
    engine.rootContext()->setContextProperty(QStringLiteral("veditBuildNumber"), VEDIT_BUILD_NUMBER); // VEDIT
    engine.loadFromModule("Drift", "Shell");
    veditTrace("qml-loaded");

    // VEDIT: a second launch's arguments arrive here; open its project, show this window.
    if (instanceServer) {
        QObject::connect(instanceServer, &QLocalServer::newConnection, &app, [instanceServer, &app] {
            while (QLocalSocket *socket = instanceServer->nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::disconnected, &app, [socket, &app] {
                    const QString payload = QString::fromUtf8(socket->readAll());
                    socket->deleteLater();
                    QStringList args{app.arguments().constFirst()};
                    for (const QString &arg : payload.split(QLatin1Char(0x0A), Qt::SkipEmptyParts))
                        args.append(arg);
                    const QUrl url = AppController::startupProjectUrlFromArguments(args);
                    if (url.isValid() && !url.isEmpty())
                        editorState.queueExternalProject(url);
                    veditBringToFront();
                });
            }
        });
    }

    bool watchingFirstFrame = false;
    for (QObject *root : engine.rootObjects())
        if (auto *window = qobject_cast<QQuickWindow *>(root)) {
            watchingFirstFrame = true;
            QObject::connect(window, &QQuickWindow::frameSwapped, window, [splash] {
                static bool once = false;
                if (once)
                    return;
                once = true;
                veditTrace("first-frame");
                // Queued: closing inside the scene graph's swap signal is not safe.
                QTimer::singleShot(0, [splash] {
                    if (splash) {
                        splash->close();
                        splash->deleteLater();
                    }
#ifdef Q_OS_WIN
                    delete cutSplash; // VEDIT: closes its window and joins its thread
                    cutSplash = nullptr;
#endif
                });
            });
        }
    if (!watchingFirstFrame && splash) {
        splash->close();
        splash->deleteLater();
    }
#ifdef Q_OS_WIN
    if (!watchingFirstFrame) {
        delete cutSplash;
        cutSplash = nullptr;
    }
#endif
    return app.exec();
}
