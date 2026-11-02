#include "UpdateChecker.h"

#include "VersionCompare.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#if __has_include("vedit_build_number.h")
#include "vedit_build_number.h" // VEDIT: the build stamp is the version
#endif
#ifndef VEDIT_BUILD_NUMBER
#define VEDIT_BUILD_NUMBER 0
#endif
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

// Configured in CMakeLists.txt (DRIFT_UPDATE_FEED_URL) and injected as a compile definition, the
// same way the addon service is, so a fork points at its own repository without touching code.
// A translation unit built outside the `drift` target gets no feed rather than a stale one.
#ifndef DRIFT_UPDATE_FEED_URL
#define DRIFT_UPDATE_FEED_URL ""
#endif

namespace {

// Long enough that the first frame, the project restore and the addon index refresh are all past
// before a socket is opened. Nothing in the app waits on this.
constexpr int kStartupDelayMs = 5000;

constexpr int kTransferTimeoutMs = 15000;

// VEDIT (조박사님: 배포): releases come from the project's own GitHub repository, and the version
// is 1.0.<build> so every published build is newer than the last. On by default since 2026-10-28
// (조박사님 lifted the no-automatic-internet rule); see enabled().
const QString kFeedUrl = QStringLiteral(DRIFT_UPDATE_FEED_URL).isEmpty()
    ? QStringLiteral("https://api.github.com/repos/cho-pd/easycuteasygo/releases/latest")
    : QStringLiteral(DRIFT_UPDATE_FEED_URL);
const QString kCurrentVersion = VEDIT_BUILD_NUMBER > 0
    ? QStringLiteral("1.0.%1").arg(VEDIT_BUILD_NUMBER)
    : QStringLiteral(DRIFT_VERSION);
// The asset the update downloads; the site's download button points at the same name.
const QString kInstallerAsset = QStringLiteral("EasyCutEasyGo-Setup-x64.exe");
// VEDIT (조박사님 2026-10-28: 회원가입 해야만 다운로드): an update is downloaded again from the
// site's sign-up gate, which logs the user in and then hands over the latest installer.
const QString kGatedDownloadUrl = QStringLiteral("https://sorigiolabs.com/dl/easycuteasygo");

QString updatesDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("updates"));
}

QString settingsKey(const char *name)
{
    return QLatin1String("updates/") + QLatin1String(name);
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
    m_skippedVersion = QSettings().value(settingsKey("skippedVersion")).toString();

    if (!supported() || !enabled())
        return;

    // VEDIT: every launch, not once a day — the result is not stored, so skipping a launch
    // would hide the header notice until the next day's check.
    QTimer::singleShot(kStartupDelayMs, this, [this] { check(false); });
}

UpdateChecker::~UpdateChecker() = default;

bool UpdateChecker::supported() const
{
    return !kFeedUrl.isEmpty();
}

bool UpdateChecker::enabled() const
{
    // VEDIT: on by default. A new key, so a "no" given to the old first-launch question
    // does not keep the notice away; the settings switch still turns it off.
    return QSettings().value(settingsKey("autoCheck"), true).toBool();
}

bool UpdateChecker::asked() const
{
    // VEDIT: the first-launch consent question is gone; Main.qml opens it only while this is false.
    return true;
}

void UpdateChecker::answerConsent(bool allow)
{
    QSettings().setValue(settingsKey("asked"), true);
    QSettings().setValue(settingsKey("autoCheck"), allow); // VEDIT
    emit enabledChanged();
    if (allow)
        QTimer::singleShot(kStartupDelayMs, this, [this] { check(false); });
}

void UpdateChecker::setDownload(const QString &state, double progress)
{
    m_downloadState = state;
    m_downloadProgress = progress;
    emit downloadChanged();
}

void UpdateChecker::downloadUpdate()
{
    if (m_installerUrl.isEmpty() || m_downloadState == QLatin1String("downloading"))
        return;
    QDir().mkpath(updatesDir());
    const QString version = m_latestVersion;
    const QString finalPath = QDir(updatesDir()).filePath(QStringLiteral("EasyCutEasyGo-Setup-%1.exe").arg(version));
    const QString partPath = finalPath + QStringLiteral(".part");
    auto *file = new QFile(partPath, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete file;
        setDownload(QStringLiteral("failed"), 0.0);
        return;
    }
    setDownload(QStringLiteral("downloading"), 0.0);
    QNetworkRequest request{QUrl(m_installerUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QLatin1String("EasyCutEasyGo/") + kCurrentVersion);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::readyRead, this, [reply, file] { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        const qint64 all = total > 0 ? total : m_installerSize;
        if (all > 0)
            setDownload(QStringLiteral("downloading"), double(got) / double(all));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, partPath, finalPath, version] {
        reply->deleteLater();
        file->write(reply->readAll());
        file->close();
        const qint64 got = file->size();
        file->deleteLater();
        const bool complete = reply->error() == QNetworkReply::NoError
            && (m_installerSize <= 0 || got == m_installerSize);
        if (!complete) {
            QFile::remove(partPath);
            setDownload(QStringLiteral("failed"), 0.0);
            return;
        }
        QFile::remove(finalPath);
        if (!QFile::rename(partPath, finalPath)) {
            setDownload(QStringLiteral("failed"), 0.0);
            return;
        }
        QSettings().setValue(settingsKey("pendingInstaller"), finalPath);
        QSettings().setValue(settingsKey("pendingVersion"), version);
        setDownload(QStringLiteral("ready"), 1.0);
    });
}

bool UpdateChecker::runPendingInstaller()
{
    QSettings settings;
    const QString path = settings.value(settingsKey("pendingInstaller")).toString();
    const QString version = settings.value(settingsKey("pendingVersion")).toString();
    if (path.isEmpty())
        return false;
    // Once only, whatever happens next: a failing installer must not keep the app from opening.
    settings.remove(settingsKey("pendingInstaller"));
    settings.remove(settingsKey("pendingVersion"));
    settings.sync();
    if (!QFileInfo::exists(path) || drift::compareVersions(kCurrentVersion, version) >= 0) {
        QFile::remove(path);
        return false;
    }
    // Quiet install over this one; the installer reopens the app when it is done.
    return QProcess::startDetached(path, {QStringLiteral("/SILENT"), QStringLiteral("/SUPPRESSMSGBOXES"),
                                          QStringLiteral("/NORESTART"), QStringLiteral("/CLOSEAPPLICATIONS")});
}

void UpdateChecker::setEnabled(bool enabled)
{
    if (enabled == this->enabled())
        return;
    QSettings().setValue(settingsKey("autoCheck"), enabled); // VEDIT
    emit enabledChanged();
}

bool UpdateChecker::checking() const
{
    return m_checking;
}

bool UpdateChecker::updateAvailable() const
{
    return !m_latestVersion.isEmpty() && m_latestVersion != m_skippedVersion;
}

QString UpdateChecker::currentVersion() const
{
    return kCurrentVersion;
}

QString UpdateChecker::latestVersion() const
{
    return m_latestVersion;
}

QString UpdateChecker::releaseNotes() const
{
    return m_releaseNotes;
}

QString UpdateChecker::releaseUrl() const
{
    return m_releaseUrl;
}

QString UpdateChecker::status() const
{
    return m_status;
}

void UpdateChecker::setChecking(bool checking)
{
    if (m_checking == checking)
        return;
    m_checking = checking;
    emit checkingChanged();
}

void UpdateChecker::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void UpdateChecker::checkNow()
{
    // Asking explicitly un-skips: the user wants to be told about whatever is out there.
    if (!m_skippedVersion.isEmpty()) {
        m_skippedVersion.clear();
        QSettings().remove(settingsKey("skippedVersion"));
    }
    check(true);
}

void UpdateChecker::skipVersion()
{
    if (m_latestVersion.isEmpty())
        return;
    m_skippedVersion = m_latestVersion;
    QSettings().setValue(settingsKey("skippedVersion"), m_skippedVersion);
    emit resultChanged();
}

void UpdateChecker::openDownloadPage()
{
    QDesktopServices::openUrl(QUrl(kGatedDownloadUrl)); // VEDIT: through the sign-up gate
    emit downloadPageOpened(); // VEDIT
}

void UpdateChecker::check(bool manual)
{
    if (m_checking || !supported())
        return;

    setChecking(true);
    setStatus(QString());

    QNetworkRequest request{QUrl(kFeedUrl)};
    // GitHub's API rejects requests that send no User-Agent, and pins response shape to an API
    // version so a future default cannot change the fields parsed below.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QLatin1String("Drift/") + kCurrentVersion);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
        reply->deleteLater();
        setChecking(false);

        // A background check that failed says nothing: being offline is not an error the user
        // asked about. Only record the attempt when it actually reached GitHub, so a laptop that
        // launches offline all week still checks the day it has a connection.
        if (reply->error() != QNetworkReply::NoError) {
            if (manual)
                setStatus(tr("Couldn’t check for updates: %1").arg(reply->errorString()));
            return;
        }

        QSettings().setValue(settingsKey("lastCheck"), QDateTime::currentDateTimeUtc());
        applyRelease(reply->readAll(), manual);
    });
}

void UpdateChecker::applyRelease(const QByteArray &json, bool manual)
{
    const QJsonObject release = QJsonDocument::fromJson(json).object();

    // releases/latest already excludes drafts and pre-releases; the checks are here so a feed
    // that ever stops doing so cannot start advertising them.
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    const bool usable = !tag.isEmpty() && !release.value(QStringLiteral("draft")).toBool()
            && !release.value(QStringLiteral("prerelease")).toBool();
    if (!usable) {
        if (manual)
            setStatus(tr("Couldn’t check for updates: unexpected response."));
        return;
    }

    const QString version = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
    if (drift::compareVersions(kCurrentVersion, version) >= 0) {
        // A previously-found update that has since been withdrawn stops being advertised.
        m_latestVersion.clear();
        m_releaseNotes.clear();
        m_releaseUrl.clear();
        emit resultChanged();
        if (manual)
            setStatus(tr("Drift %1 is the latest version.").arg(kCurrentVersion));
        return;
    }

    m_latestVersion = version;
    m_releaseNotes = release.value(QStringLiteral("body")).toString();
    m_releaseUrl = release.value(QStringLiteral("html_url")).toString();
    // VEDIT: the installer this release carries, for the in-app update.
    m_installerUrl.clear();
    m_installerSize = 0;
    for (const QJsonValue &a : release.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = a.toObject();
        if (asset.value(QStringLiteral("name")).toString() == kInstallerAsset) {
            m_installerUrl = asset.value(QStringLiteral("browser_download_url")).toString();
            m_installerSize = qint64(asset.value(QStringLiteral("size")).toDouble());
        }
    }
    emit resultChanged();
    if (manual)
        setStatus(tr("Drift %1 is available.").arg(version));
}
