#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QShowEvent>
#include <QWindow>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QQueue>

class CustomWebPage : public QWebEnginePage {
    Q_OBJECT
public:
    explicit CustomWebPage(QObject *parent = nullptr) : QWebEnginePage(parent) {}

signals:
    void rpcCall(const QString &seq, const QString &functionName, const QString &reqJSON);

protected:
    bool javaScriptPrompt(const QUrl &securityOrigin, const QString &msg, const QString &defaultValue, QString *result) override;
    void javaScriptConsoleMessage(QWebEnginePage::JavaScriptConsoleMessageLevel level, const QString &message, int lineNumber, const QString &sourceID) override;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void handleRPCCall(const QString &seq, const QString &functionName, const QString &reqJSON);
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    void handleProcessOutput();
    void handleProcessError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QString getLauncherDir() const;
    void resolve(const QString &seq, int status, const QString &result);
    void evalJS(const QString &js);
    void logToConsole(const QString &message, const QString &type = "system");
    QString escapeJS(const QString &val) const;
    QString scanForExecutables(const QString &dirPath);

    void getHomeDir(const QString &seq);
    void saveConfig(const QString &seq, const QJsonArray &args);
    void getConfig(const QString &seq, const QJsonArray &args);
    void getInstalledApps(const QString &seq);
    void detectInstalledApp(const QString &seq, const QJsonArray &args);
    void downloadFile(const QString &seq, const QJsonArray &args);
    void buildRepo(const QString &seq, const QJsonArray &args);
    void launchApp(const QString &seq, const QJsonArray &args);
    void uninstallApp(const QString &seq, const QJsonArray &args);
    void resetLauncherCache(const QString &seq);

    void downloadViaCurl(const QString &urlStr, const QString &filePath, const QString &repoName, const QString &filename, const QString &seq);
    void downloadViaWget(const QString &urlStr, const QString &filePath, const QString &repoName, const QString &filename, const QString &seq);
    void finalizeDownload(const QString &filePath, const QString &repoName, const QString &filename, const QString &seq);

    void startNextBuildStep();
    void runGitClone();
    void runGitFallbackZip();
    void runUnzip(const QString &zipPath);
    void runCMakeConfigure();
    void runCMakeBuild();
    void runMakefileBuild();
    void runScriptBuild(const QString &scriptName);
    void finalizeBuild();
    void failBuild(const QString &errorMessage);

    QWebEngineView *m_view;
    CustomWebPage *m_page;

    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply;
    QFile *m_downloadFile;
    QString m_downloadSeq;
    QString m_downloadRepoName;
    QString m_downloadFileName;
    QString m_downloadUrl;
    QString m_downloadTargetFilePath;

    QProcess *m_process;
    QString m_buildSeq;
    QString m_buildRepoName;
    QString m_buildCloneUrl;
    QString m_sourcesDir;
    QString m_appsDir;
    
    enum BuildStep {
        Idle,
        Cloning,
        Unzipping,
        CMakeConfiguring,
        CMakeBuilding,
        MakefileBuilding,
        ScriptBuilding,
        CopyingBinary
    };
    BuildStep m_currentBuildStep;
    QString m_activeScript;
    bool m_buildSuccess;
};

#endif
