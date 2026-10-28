#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Asks GitHub once a day whether there is a newer release than this build, and raises a badge in
// the header when there is. It never downloads or installs anything: the four packaging formats
// (AppImage, Windows installer, Arch package, Flatpak) have nothing in common to install *into*,
// so the honest end of the flow is the release page in the user's browser.
//
// Builds that a package manager owns are configured with an empty DRIFT_UPDATE_FEED_URL and get
// none of this — see supported(). Nagging a Flatpak or pacman user about a version their package
// manager will fetch on its own schedule is noise they cannot act on.
class UpdateChecker : public QObject
{
    Q_OBJECT
    // False when the build was configured with an empty feed URL. The badge and the settings row
    // are both hidden then, rather than showing a control that can never do anything.
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY resultChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY resultChanged)
    // The release's Markdown body, rendered as Markdown by the dialog.
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY resultChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY resultChanged)
    // Result of the last check in one line, for the settings row. Empty until something is worth
    // saying; a failed background check says nothing at all.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // VEDIT (조박사님: 배포·자동 업데이트): the first launch asks whether to be told about new
    // versions — the app is otherwise fully offline, so nothing is fetched before a yes.
    Q_PROPERTY(bool asked READ asked NOTIFY enabledChanged)
    // The installer of the newer version: "idle", "downloading", "ready" (installs at the next
    // start), "failed". Progress 0..1 while downloading.
    Q_PROPERTY(QString downloadState READ downloadState NOTIFY downloadChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY resultChanged)

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    bool supported() const;
    bool enabled() const;
    void setEnabled(bool enabled);
    bool checking() const;
    bool updateAvailable() const;
    QString currentVersion() const;
    QString latestVersion() const;
    QString releaseNotes() const;
    QString releaseUrl() const;
    QString status() const;

    // User pressed "Check now": ignores the once-a-day throttle and the skipped version, and
    // reports failures and "you are up to date" through status().
    Q_INVOKABLE void checkNow();

    // Stops this version being advertised again. The next release clears it by being newer.
    Q_INVOKABLE void skipVersion();

    Q_INVOKABLE void openDownloadPage();

    // VEDIT
    bool asked() const;
    Q_INVOKABLE void answerConsent(bool allow);
    QString downloadState() const { return m_downloadState; }
    double downloadProgress() const { return m_downloadProgress; }
    bool canInstall() const { return !m_installerUrl.isEmpty(); }
    // Fetches the new installer in the background; it is run the next time the app starts.
    Q_INVOKABLE void downloadUpdate();
    // Called first thing in main(): a downloaded installer for a newer version is started (quietly;
    // it reopens the app when done) and true is returned so the app closes at once.
    static bool runPendingInstaller();

signals:
    void enabledChanged();
    void checkingChanged();
    void resultChanged();
    void statusChanged();
    void downloadChanged(); // VEDIT

private:
    void check(bool manual);
    void applyRelease(const QByteArray &json, bool manual);
    void setChecking(bool checking);
    void setStatus(const QString &status);

    bool m_checking = false;
    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_releaseUrl;
    QString m_skippedVersion;
    QString m_status;
    QNetworkAccessManager *m_network = nullptr;
    // VEDIT
    QString m_installerUrl;
    qint64 m_installerSize = 0;
    QString m_downloadState = QStringLiteral("idle");
    double m_downloadProgress = 0.0;
    void setDownload(const QString &state, double progress);
};
