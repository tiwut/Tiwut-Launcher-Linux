#include "mainwindow.h"
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStandardPaths>
#include <QUrl>
#include <QDebug>
#include <QCoreApplication>
#include <QWebEngineScriptCollection>
#include <QThread>
#include <QWebEngineSettings>

bool CustomWebPage::javaScriptPrompt(const QUrl &securityOrigin, const QString &msg, const QString &defaultValue, QString *result) {
    Q_UNUSED(securityOrigin);
    Q_UNUSED(defaultValue);

    if (msg.startsWith("rpc:")) {
        QString jsonStr = msg.mid(4);
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            QString seq = obj["seq"].toString();
            QString functionName = obj["functionName"].toString();
            
            QString reqJSON = obj["req"].toString();
            emit rpcCall(seq, functionName, reqJSON);
        }
        if (result) {
            *result = "";
        }
        return true; 
    }
    return QWebEnginePage::javaScriptPrompt(securityOrigin, msg, defaultValue, result);
}

void CustomWebPage::javaScriptConsoleMessage(QWebEnginePage::JavaScriptConsoleMessageLevel level, const QString &message, int lineNumber, const QString &sourceID) {
    Q_UNUSED(level);
    Q_UNUSED(lineNumber);
    Q_UNUSED(sourceID);
    qDebug() << "[JS Console]" << message;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_currentReply(nullptr)
    , m_downloadFile(nullptr)
    , m_process(nullptr)
    , m_currentBuildStep(Idle)
    , m_buildSuccess(false)
{
    
    setAttribute(Qt::WA_TranslucentBackground, true);
    setStyleSheet("background: transparent;");

    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);

    m_page = new CustomWebPage(this);
    m_view = new QWebEngineView(this);
    m_view->setPage(m_page);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    m_view->setAttribute(Qt::WA_TranslucentBackground, true);
    m_view->setStyleSheet("background: transparent;");
    m_page->setBackgroundColor(Qt::transparent);

    layout->addWidget(m_view);
    setCentralWidget(centralWidget);

    QWebEngineScript script;
    QString shimCode = R"(
        window.webkit = {
            messageHandlers: {
                rpc: {
                    postMessage: function(msg) {
                        window.prompt("rpc:" + JSON.stringify(msg));
                    }
                }
            }
        };
        window.get_os = function() {
            return Promise.resolve("linux");
        };
    )";
    script.setSourceCode(shimCode.toUtf8());
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(false);
    m_page->profile()->scripts()->insert(script);

    connect(m_page, &CustomWebPage::rpcCall, this, &MainWindow::handleRPCCall);

    m_networkManager = new QNetworkAccessManager(this);

    QString exeDir = QCoreApplication::applicationDirPath();
    QString currentDir = QDir::currentPath();

    QStringList lookupPaths = {
        exeDir + "/src/index.html",
        exeDir + "/index.html",
        exeDir + "/../src/index.html",
        exeDir + "/../../src/index.html",
        currentDir + "/src/index.html",
        currentDir + "/index.html",
        currentDir + "/../src/index.html",
        currentDir + "/../../src/index.html"
    };

    QString htmlPath;
    for (const QString &path : lookupPaths) {
        if (QFile::exists(path)) {
            htmlPath = path;
            break;
        }
    }

    if (!htmlPath.isEmpty()) {
        m_view->load(QUrl::fromLocalFile(htmlPath));
    } else {
        QString errorHTML = R"(
            <html>
                <body style="background:#0f172a; color:#f3f4f6; font-family:sans-serif; text-align:center; padding:100px;">
                    <h1>Tiwut Launcher Error</h1>
                    <p>Could not locate the frontend file: 'src/index.html'</p>
                 body>
            </html>
        )";
        m_view->setHtml(errorHTML);
    }
}

MainWindow::~MainWindow() {
    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
    }
}

QString MainWindow::getLauncherDir() const {
    QString path = QDir::homePath() + "/.local/share/TiwutLauncher";
    QDir().mkpath(path);
    return path;
}

void MainWindow::resolve(const QString &seq, int status, const QString &result) {
    QString js = QString("window.rpc_resolve('%1', %2, '%3')")
                     .arg(seq)
                     .arg(status)
                     .arg(escapeJS(result));
    evalJS(js);
}

void MainWindow::evalJS(const QString &js) {
    m_view->page()->runJavaScript(js);
}

void MainWindow::logToConsole(const QString &message, const QString &type) {
    QString js = QString("window.onBuildLog('[%1] %2')")
                     .arg(type.toUpper())
                     .arg(escapeJS(message));
    evalJS(js);
}

QString MainWindow::escapeJS(const QString &val) const {
    QString escaped = val;
    escaped.replace("\\", "\\\\")
           .replace("\"", "\\\"")
           .replace("\'", "\\'")
           .replace("\n", "\\n")
           .replace("\r", "\\r");
    return escaped;
}

void MainWindow::handleRPCCall(const QString &seq, const QString &functionName, const QString &reqJSON) {
    QJsonDocument doc = QJsonDocument::fromJson(reqJSON.toUtf8());
    QJsonArray args;
    if (!doc.isNull() && doc.isArray()) {
        args = doc.array();
    }

    if (functionName == "get_home_dir") {
        getHomeDir(seq);
    } else if (functionName == "save_config") {
        saveConfig(seq, args);
    } else if (functionName == "get_config") {
        getConfig(seq, args);
    } else if (functionName == "get_installed_apps") {
        getInstalledApps(seq);
    } else if (functionName == "detect_installed_app") {
        detectInstalledApp(seq, args);
    } else if (functionName == "download_file") {
        downloadFile(seq, args);
    } else if (functionName == "build_repo") {
        buildRepo(seq, args);
    } else if (functionName == "launch_app") {
        launchApp(seq, args);
    } else if (functionName == "uninstall_app") {
        uninstallApp(seq, args);
    } else if (functionName == "reset_launcher_cache") {
        resetLauncherCache(seq);
    } else {
        resolve(seq, 1, "Function not found");
    }
}

void MainWindow::getHomeDir(const QString &seq) {
    resolve(seq, 0, QDir::homePath());
}

void MainWindow::saveConfig(const QString &seq, const QJsonArray &args) {
    if (args.size() < 2) {
        resolve(seq, 1, "Invalid parameters");
        return;
    }
    QString key = args[0].toString();
    QString val = args[1].toString();
    
    QString filePath = getLauncherDir() + "/" + key + ".txt";
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << val;
        file.close();
        resolve(seq, 0, "Success");
    } else {
        resolve(seq, 1, "Failed to write config file");
    }
}

void MainWindow::getConfig(const QString &seq, const QJsonArray &args) {
    if (args.size() < 1) {
        resolve(seq, 0, "");
        return;
    }
    QString key = args[0].toString();
    QString filePath = getLauncherDir() + "/" + key + ".txt";
    
    QFile file(filePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString content = in.readAll();
        file.close();
        resolve(seq, 0, content);
    } else {
        resolve(seq, 0, "");
    }
}

void MainWindow::getInstalledApps(const QString &seq) {
    QString filePath = getLauncherDir() + "/installed.txt";
    QFile file(filePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString content = in.readAll();
        file.close();
        resolve(seq, 0, content);
    } else {
        resolve(seq, 0, "");
    }
}

void MainWindow::detectInstalledApp(const QString &seq, const QJsonArray &args) {
    if (args.size() < 1) {
        resolve(seq, 1, "Missing app name");
        return;
    }
    QString repoName = args[0].toString();
    QString localDir = getLauncherDir() + "/apps/" + repoName;
    
    QString binaryPath = scanForExecutables(localDir);
    resolve(seq, 0, binaryPath);
}

QString MainWindow::scanForExecutables(const QString &dirPath) {
    QDir dir(dirPath);
    if (!dir.exists()) return "";

    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Executable);
    
    for (const QFileInfo &info : entries) {
        if (info.isFile() && info.isExecutable() && info.suffix().isEmpty() && !info.absoluteFilePath().contains("CMakeFiles")) {
            return info.absoluteFilePath();
        }
    }

    QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &subdir : subdirs) {
        QString res = scanForExecutables(subdir.absoluteFilePath());
        if (!res.isEmpty()) return res;
    }

    return "";
}

void MainWindow::downloadFile(const QString &seq, const QJsonArray &args) {
    if (args.size() < 3) {
        resolve(seq, 1, "Invalid download arguments");
        return;
    }
    m_downloadUrl = args[0].toString();
    m_downloadFileName = args[1].toString();
    m_downloadRepoName = args[2].toString();
    m_downloadSeq = seq;

    QString targetDir = getLauncherDir() + "/apps/" + m_downloadRepoName;
    QDir().mkpath(targetDir);
    m_downloadTargetFilePath = targetDir + "/" + m_downloadFileName;

    logToConsole("Starting native download from " + m_downloadUrl + "...", "system");

    QNetworkRequest request((QUrl(m_downloadUrl)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_downloadFile = new QFile(m_downloadTargetFilePath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        logToConsole("Failed to open local file for writing. Falling back to Curl...", "warning");
        delete m_downloadFile;
        m_downloadFile = nullptr;
        downloadViaCurl(m_downloadUrl, m_downloadTargetFilePath, m_downloadRepoName, m_downloadFileName, m_downloadSeq);
        return;
    }

    m_currentReply = m_networkManager->get(request);
    connect(m_currentReply, &QNetworkReply::downloadProgress, this, &MainWindow::onDownloadProgress);
    connect(m_currentReply, &QNetworkReply::finished, this, &MainWindow::onDownloadFinished);
    connect(m_currentReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadFile && m_currentReply) {
            m_downloadFile->write(m_currentReply->readAll());
        }
    });
}

void MainWindow::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal > 0) {
        int percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
        QString js = QString("window.onDownloadProgress(%1, %2, 100)").arg(percent).arg(percent);
        evalJS(js);
    }
}

void MainWindow::onDownloadFinished() {
    if (!m_currentReply) return;

    QNetworkReply::NetworkError err = m_currentReply->error();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }

    if (err == QNetworkReply::NoError) {
        logToConsole("Native download completed successfully!", "system");
        finalizeDownload(m_downloadTargetFilePath, m_downloadRepoName, m_downloadFileName, m_downloadSeq);
    } else {
        logToConsole("Native download failed. Trying fallback Way 2: Curl...", "warning");
        downloadViaCurl(m_downloadUrl, m_downloadTargetFilePath, m_downloadRepoName, m_downloadFileName, m_downloadSeq);
    }
}

void MainWindow::downloadViaCurl(const QString &urlStr, const QString &filePath, const QString &repoName, const QString &filename, const QString &seq) {
    logToConsole("Initiating Way 2: curl subprocess download...", "system");

    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProgram("curl");
    m_process->setArguments({"-L", "-o", filePath, "-A", "Mozilla/5.0", urlStr});

    connect(m_process, &QProcess::finished, this, [this, filePath, repoName, filename, seq, urlStr](int exitCode) {
        QFileInfo info(filePath);
        if (exitCode == 0 && info.exists() && info.size() > 1000) {
            logToConsole("Way 2: curl download succeeded!", "system");
            finalizeDownload(filePath, repoName, filename, seq);
        } else {
            logToConsole("Way 2: curl download failed. Trying fallback Way 3: Wget...", "warning");
            downloadViaWget(urlStr, filePath, repoName, filename, seq);
        }
    });

    m_process->start();
}

void MainWindow::downloadViaWget(const QString &urlStr, const QString &filePath, const QString &repoName, const QString &filename, const QString &seq) {
    logToConsole("Initiating Way 3: wget subprocess download...", "system");

    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProgram("wget");
    m_process->setArguments({"-O", filePath, "-U", "Mozilla/5.0", urlStr});

    connect(m_process, &QProcess::finished, this, [this, filePath, repoName, filename, seq](int exitCode) {
        QFileInfo info(filePath);
        if (exitCode == 0 && info.exists() && info.size() > 1000) {
            logToConsole("Way 3: wget download succeeded!", "system");
            finalizeDownload(filePath, repoName, filename, seq);
        } else {
            logToConsole("All download methods (Native, curl, wget) failed!", "error");
            QString js = QString("window.onDownloadFailed('%1', 'Download failed on all fallbacks.')").arg(repoName);
            evalJS(js);
            resolve(seq, 1, "Download failed");
        }
    });

    m_process->start();
}

void MainWindow::finalizeDownload(const QString &filePath, const QString &repoName, const QString &filename, const QString &seq) {
    QString finalPath = filePath;
    QString targetDir = QFileInfo(filePath).absolutePath();

    bool installedSuccessfully = false;

    if (filename.endsWith(".appimage", Qt::CaseInsensitive)) {
        logToConsole("AppImage file detected! Configuring permissions...", "system");
        
        QFile::setPermissions(filePath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                         QFile::ReadUser  | QFile::ExeUser  | QFile::ReadGroup | QFile::ExeGroup);
        
        QProcess testProcess;
        testProcess.start(filePath, {"--appimage-version"});
        testProcess.waitForFinished(1000);
        
        if (testProcess.exitCode() == 0) {
            logToConsole("AppImage verified successfully.", "system");
            installedSuccessfully = true;
        } else {
            logToConsole("AppImage direct run failed (possibly missing FUSE libfuse2). Initiating fail-safe: extracting AppImage...", "warning");
            
            QProcess extract;
            extract.setWorkingDirectory(targetDir);
            extract.start(filePath, {"--appimage-extract"});
            extract.waitForFinished();
            
            QString squashPath = targetDir + "/squashfs-root";
            if (QDir(squashPath).exists()) {
                logToConsole("AppImage extracted successfully to squashfs-root.", "system");
                QString appRunPath = squashPath + "/AppRun";
                QFile::setPermissions(appRunPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                                 QFile::ReadUser  | QFile::ExeUser  | QFile::ReadGroup | QFile::ExeGroup);
                finalPath = appRunPath;
                installedSuccessfully = true;
            } else {
                logToConsole("AppImage extraction failed. Reverting to direct AppImage launcher.", "warning");
            }
        }
    }
    
    else if (filename.endsWith(".deb", Qt::CaseInsensitive)) {
        logToConsole("Debian package (.deb) detected! Commencing local non-root installation...", "system");
        
        QString destDir = targetDir + "/deb_extracted";
        QDir().mkpath(destDir);
        
        logToConsole("Tier A: trying dpkg-deb extraction...", "system");
        QProcess dpkg;
        dpkg.start("dpkg-deb", {"-x", filePath, destDir});
        dpkg.waitForFinished();
        
        if (dpkg.exitCode() != 0 || QDir(destDir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
            logToConsole("dpkg-deb failed. Tier B: trying ar + tar extraction...", "warning");
            
            QString tempArDir = targetDir + "/ar_temp";
            QDir().mkpath(tempArDir);
            
            QProcess ar;
            ar.setWorkingDirectory(tempArDir);
            ar.start("ar", {"x", filePath});
            ar.waitForFinished();
            
            QStringList dataFiles = QDir(tempArDir).entryList({"data.tar.*"}, QDir::Files);
            if (!dataFiles.isEmpty()) {
                QString dataArchive = tempArDir + "/" + dataFiles.first();
                QProcess tar;
                tar.start("tar", {"-xf", dataArchive, "-C", destDir});
                tar.waitForFinished();
            }
            
            QDir(tempArDir).removeRecursively();
        }
        
        QString binary = scanForExecutables(destDir);
        if (!binary.isEmpty()) {
            logToConsole("Debian package installed locally. Binary found at: " + binary, "system");
            finalPath = binary;
            installedSuccessfully = true;
        } else {
            logToConsole("Failed to locate executable in deb package.", "warning");
        }
    }
    
    else if (filename.endsWith(".rpm", Qt::CaseInsensitive)) {
        logToConsole("RPM package (.rpm) detected! Commencing local non-root installation...", "system");
        
        QString destDir = targetDir + "/rpm_extracted";
        QDir().mkpath(destDir);
        
        logToConsole("Tier A: trying rpm2cpio extraction...", "system");
        QProcess rpm;
        rpm.start("sh", {"-c", QString("rpm2cpio %1 | cpio -idm -D %2").arg(filePath).arg(destDir)});
        rpm.waitForFinished();
        
        if (rpm.exitCode() != 0 || QDir(destDir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
            logToConsole("rpm2cpio failed. Tier B: trying 7z fallback...", "warning");
            QProcess extract7z;
            extract7z.start("7z", {"x", filePath, "-o" + destDir, "-y"});
            extract7z.waitForFinished();
        }
        
        QString binary = scanForExecutables(destDir);
        if (!binary.isEmpty()) {
            logToConsole("RPM package installed locally. Binary found at: " + binary, "system");
            finalPath = binary;
            installedSuccessfully = true;
        } else {
            logToConsole("Failed to locate executable in RPM package.", "warning");
        }
    }
    
    else if (filename.endsWith(".flatpak", Qt::CaseInsensitive)) {
        logToConsole("Flatpak bundle (.flatpak) detected! Commencing user-context installation...", "system");
        
        QProcess flatpak;
        flatpak.start("flatpak", {"install", "--user", "-y", "--bundle", filePath});
        flatpak.waitForFinished();
        
        if (flatpak.exitCode() == 0) {
            logToConsole("Flatpak bundle installed successfully in user registry.", "system");
            installedSuccessfully = true;
            
            QString appid = filename.left(filename.lastIndexOf(".flatpak"));
            QString scriptPath = targetDir + "/launch_flatpak.sh";
            QFile script(scriptPath);
            if (script.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&script);
                out << "#!/bin/sh\n";
                out << "flatpak run " << appid << " \"$@\"\n";
                script.close();
                QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                                 QFile::ReadUser  | QFile::ExeUser  | QFile::ReadGroup | QFile::ExeGroup);
                finalPath = scriptPath;
            }
        } else {
            logToConsole("Flatpak bundle installation failed. Checking flatpak availability...", "error");
        }
    }
    
    else if (filename.endsWith(".zip", Qt::CaseInsensitive)) {
        logToConsole("ZIP file detected! Commencing extraction...", "system");
        
        QProcess unzip;
        unzip.start("/usr/bin/unzip", {"-o", filePath, "-d", targetDir});
        unzip.waitForFinished();
        
        if (unzip.exitCode() != 0) {
            logToConsole("unzip failed. Initiating fail-safe: Python-based ZIP extractor...", "warning");
            QProcess pyZip;
            pyZip.start("python3", {"-c", QString("import zipfile; zipfile.ZipFile('%1').extractall('%2')").arg(filePath).arg(targetDir)});
            pyZip.waitForFinished();
        }
        
        QString binary = scanForExecutables(targetDir);
        if (!binary.isEmpty()) {
            finalPath = binary;
            installedSuccessfully = true;
        }
    }
    else if (filename.endsWith(".tar.gz", Qt::CaseInsensitive) || filename.endsWith(".tgz", Qt::CaseInsensitive) || filename.endsWith(".tar.xz", Qt::CaseInsensitive)) {
        logToConsole("Tarball archive detected! Commencing extraction...", "system");
        
        QProcess tar;
        tar.start("/usr/bin/tar", {"-xf", filePath, "-C", targetDir});
        tar.waitForFinished();
        
        if (tar.exitCode() != 0) {
            logToConsole("tar failed. Initiating fail-safe: Python-based Tarball extractor...", "warning");
            QProcess pyTar;
            pyTar.start("python3", {"-c", QString("import tarfile; tarfile.open('%1').extractall('%2')").arg(filePath).arg(targetDir)});
            pyTar.waitForFinished();
        }
        
        QString binary = scanForExecutables(targetDir);
        if (!binary.isEmpty()) {
            finalPath = binary;
            installedSuccessfully = true;
        }
    }

    QFile::setPermissions(finalPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                     QFile::ReadUser  | QFile::ExeUser  | QFile::ReadGroup | QFile::ExeGroup);

    QString completeJS = QString("window.onDownloadComplete('%1', '%2', '%3')")
                            .arg(repoName)
                            .arg(escapeJS(finalPath))
                            .arg(escapeJS(filename));
    evalJS(completeJS);
    resolve(seq, 0, "Download completed");
}

void MainWindow::buildRepo(const QString &seq, const QJsonArray &args) {
    if (args.size() < 2) {
        resolve(seq, 1, "Invalid build arguments");
        return;
    }
    m_buildCloneUrl = args[0].toString();
    m_buildRepoName = args[1].toString();
    m_buildSeq = seq;

    m_sourcesDir = getLauncherDir() + "/sources/" + m_buildRepoName;
    m_appsDir = getLauncherDir() + "/apps/" + m_buildRepoName;

    QDir(m_sourcesDir).removeRecursively();
    QDir(m_appsDir).removeRecursively();
    QDir().mkpath(m_sourcesDir);
    QDir().mkpath(m_appsDir);

    logToConsole("Cloning repository: " + m_buildCloneUrl + "...", "system");

    m_currentBuildStep = Cloning;
    runGitClone();
}

void MainWindow::runGitClone() {
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProgram("git");
    m_process->setArguments({"clone", m_buildCloneUrl, m_sourcesDir});

    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &MainWindow::handleProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::handleProcessFinished);

    m_process->start();
}

void MainWindow::runGitFallbackZip() {
    logToConsole("Git clone failed. Trying Way 2: Downloading source ZIP archive...", "warning");
    
    m_currentBuildStep = Unzipping;
    QString zipUrl = "https://github.com/" + m_buildRepoName + "/archive/refs/heads/main.zip";
    QString zipPath = getLauncherDir() + "/sources/" + m_buildRepoName + "_source.zip";

    logToConsole("Downloading zip from: " + zipUrl, "system");

    QNetworkRequest request((QUrl(zipUrl)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_downloadFile = new QFile(zipPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        failBuild("Could not write source ZIP archive file.");
        return;
    }

    m_currentReply = m_networkManager->get(request);
    connect(m_currentReply, &QNetworkReply::finished, this, [this, zipPath, zipUrl]() {
        m_currentReply->deleteLater();
        m_currentReply = nullptr;

        if (m_downloadFile) {
            m_downloadFile->close();
            delete m_downloadFile;
            m_downloadFile = nullptr;
        }

        QFileInfo info(zipPath);
        if (info.exists() && info.size() > 2000) {
            logToConsole("Source ZIP downloaded. Commencing extraction...", "system");
            runUnzip(zipPath);
        } else {
            
            logToConsole("Main branch ZIP failed. Trying master branch ZIP...", "warning");
            QString masterUrl = "https://github.com/" + m_buildRepoName + "/archive/refs/heads/master.zip";
            QNetworkRequest req2((QUrl(masterUrl)));
            req2.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");
            req2.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            
            m_downloadFile = new QFile(zipPath);
            m_downloadFile->open(QIODevice::WriteOnly);
            
            m_currentReply = m_networkManager->get(req2);
            connect(m_currentReply, &QNetworkReply::finished, this, [this, zipPath]() {
                m_currentReply->deleteLater();
                m_currentReply = nullptr;
                if (m_downloadFile) {
                    m_downloadFile->close();
                    delete m_downloadFile;
                    m_downloadFile = nullptr;
                }

                QFileInfo masterInfo(zipPath);
                if (masterInfo.exists() && masterInfo.size() > 2000) {
                    runUnzip(zipPath);
                } else {
                    failBuild("Could not locate master or main branch source code ZIP archive.");
                }
            });
        }
    });

    connect(m_currentReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadFile && m_currentReply) {
            m_downloadFile->write(m_currentReply->readAll());
        }
    });
}

void MainWindow::runUnzip(const QString &zipPath) {
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProgram("unzip");
    
    m_process->setArguments({"-o", zipPath, "-d", getLauncherDir() + "/sources"});

    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &MainWindow::handleProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, zipPath](int exitCode) {
        QFile::remove(zipPath);
        if (exitCode == 0) {
            
            QDir sourcesDir(getLauncherDir() + "/sources");
            QStringList entries = sourcesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            
            QString shortRepo = m_buildRepoName.split('/').last();
            
            for (const QString &dirName : entries) {
                if (dirName.startsWith(shortRepo, Qt::CaseInsensitive) && dirName != shortRepo) {
                    QDir(m_sourcesDir).removeRecursively();
                    sourcesDir.rename(dirName, m_buildRepoName);
                    break;
                }
            }

            logToConsole("Source extraction completed successfully.", "system");
            m_currentBuildStep = Idle; 
            startNextBuildStep();
        } else {
            failBuild("Extraction of zip archive failed.");
        }
    });

    m_process->start();
}

void MainWindow::startNextBuildStep() {
    bool hasCMake = QFile::exists(m_sourcesDir + "/CMakeLists.txt");
    bool hasMakefile = QFile::exists(m_sourcesDir + "/Makefile");

    QStringList buildScripts = {"build.sh", "install.sh", "compile.sh", "make.sh", "setup.sh"};
    m_activeScript = "";
    for (const QString &script : buildScripts) {
        if (QFile::exists(m_sourcesDir + "/" + script)) {
            m_activeScript = script;
            break;
        }
    }

    if (hasCMake) {
        m_currentBuildStep = CMakeConfiguring;
        runCMakeConfigure();
    } else if (!m_activeScript.isEmpty()) {
        m_currentBuildStep = ScriptBuilding;
        runScriptBuild(m_activeScript);
    } else if (hasMakefile) {
        m_currentBuildStep = MakefileBuilding;
        runMakefileBuild();
    } else {
        
        logToConsole("No build configuration detected. Searching for pre-compiled binaries...", "warning");
        m_currentBuildStep = CopyingBinary;
        finalizeBuild();
    }
}

void MainWindow::runCMakeConfigure() {
    logToConsole("CMakeLists.txt detected! Configuring CMake build...", "system");

    QString buildDir = m_sourcesDir + "/build";
    QDir().mkpath(buildDir);

    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProgram("cmake");
    m_process->setArguments({"-B", buildDir, "-S", m_sourcesDir, "-DCMAKE_BUILD_TYPE=Release"});

    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &MainWindow::handleProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::handleProcessFinished);

    m_process->start();
}

void MainWindow::runCMakeBuild() {
    logToConsole("CMake configuration succeeded. Commencing compilation...", "system");

    QString buildDir = m_sourcesDir + "/build";

    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProgram("cmake");
    m_process->setArguments({"--build", buildDir, "--config", "Release"});

    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &MainWindow::handleProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::handleProcessFinished);

    m_process->start();
}

void MainWindow::runMakefileBuild() {
    logToConsole("Makefile detected! Running make compilation...", "system");

    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProgram("make");
    m_process->setArguments({"-C", m_sourcesDir, "-j" + QString::number(qMax(1, QThread::idealThreadCount()))});

    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &MainWindow::handleProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::handleProcessFinished);

    m_process->start();
}

void MainWindow::runScriptBuild(const QString &scriptName) {
    logToConsole("Build script " + scriptName + " detected! Compiling via script...", "system");

    QString scriptPath = m_sourcesDir + "/" + scriptName;
    QFile::setPermissions(scriptPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadUser  | QFile::ExeUser  | QFile::ReadGroup | QFile::ExeGroup);

    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
    }

    m_process = new QProcess(this);
    m_process->setProgram("sh");
    m_process->setArguments({scriptPath});
    m_process->setWorkingDirectory(m_sourcesDir);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &MainWindow::handleProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &MainWindow::handleProcessFinished);

    m_process->start();
}

void MainWindow::handleProcessOutput() {
    if (!m_process) return;
    QByteArray out = m_process->readAllStandardOutput();
    QString text = QString::fromUtf8(out);
    QStringList lines = text.split('\n');
    for (const QString &line : lines) {
        if (!line.trimmed().isEmpty()) {
            evalJS(QString("window.onBuildLog('%1')").arg(escapeJS(line)));
        }
    }
}

void MainWindow::handleProcessError() {
    if (!m_process) return;
    QByteArray err = m_process->readAllStandardError();
    QString text = QString::fromUtf8(err);
    QStringList lines = text.split('\n');
    for (const QString &line : lines) {
        if (!line.trimmed().isEmpty()) {
            evalJS(QString("window.onBuildLog('%1')").arg(escapeJS(line)));
        }
    }
}

void MainWindow::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    Q_UNUSED(exitStatus);

    if (m_currentBuildStep == Cloning) {
        if (exitCode == 0) {
            logToConsole("Repository cloned successfully.", "system");
            startNextBuildStep();
        } else {
            runGitFallbackZip();
        }
    } else if (m_currentBuildStep == CMakeConfiguring) {
        if (exitCode == 0) {
            m_currentBuildStep = CMakeBuilding;
            runCMakeBuild();
        } else {
            
            bool hasMakefile = QFile::exists(m_sourcesDir + "/Makefile");
            if (hasMakefile) {
                logToConsole("CMake configure failed. Trying Makefile fallback...", "warning");
                m_currentBuildStep = MakefileBuilding;
                runMakefileBuild();
            } else if (!m_activeScript.isEmpty()) {
                logToConsole("CMake configure failed. Trying build script fallback...", "warning");
                m_currentBuildStep = ScriptBuilding;
                runScriptBuild(m_activeScript);
            } else {
                failBuild("CMake configuration failed.");
            }
        }
    } else if (m_currentBuildStep == CMakeBuilding) {
        if (exitCode == 0) {
            m_currentBuildStep = CopyingBinary;
            finalizeBuild();
        } else {
            failBuild("CMake compilation failed.");
        }
    } else if (m_currentBuildStep == MakefileBuilding) {
        if (exitCode == 0) {
            m_currentBuildStep = CopyingBinary;
            finalizeBuild();
        } else {
            failBuild("Make compilation failed.");
        }
    } else if (m_currentBuildStep == ScriptBuilding) {
        if (exitCode == 0) {
            m_currentBuildStep = CopyingBinary;
            finalizeBuild();
        } else {
            failBuild("Build script execution failed.");
        }
    }
}

void MainWindow::finalizeBuild() {
    logToConsole("Locating compiled executable binary...", "system");

    QString buildPath = m_sourcesDir + "/build";
    QString binaryPath = scanForExecutables(buildPath);
    if (binaryPath.isEmpty()) {
        binaryPath = scanForExecutables(m_sourcesDir);
    }

    if (binaryPath.isEmpty()) {
        failBuild("Could not locate any compiled binary executable!");
        return;
    }

    QFileInfo info(binaryPath);
    QString destFilePath = m_appsDir + "/" + info.fileName();

    QFile::remove(destFilePath); 
    if (QFile::copy(binaryPath, destFilePath)) {
        
        QFile::setPermissions(destFilePath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                            QFile::ReadUser  | QFile::ExeUser  | QFile::ReadGroup | QFile::ExeGroup);

        logToConsole("App compiled and installed successfully to: " + destFilePath, "system");
        
        QString completeJS = QString("window.onBuildComplete('%1', '%2')")
                                 .arg(m_buildRepoName)
                                 .arg(escapeJS(destFilePath));
        evalJS(completeJS);
        resolve(m_buildSeq, 0, "Build completed");
    } else {
        failBuild("Failed to copy compiled binary to local apps directory.");
    }
}

void MainWindow::failBuild(const QString &errorMessage) {
    logToConsole("Build failed: " + errorMessage, "error");
    QString js = QString("window.onBuildFailed('%1', '%2')").arg(m_buildRepoName).arg(escapeJS(errorMessage));
    evalJS(js);
    resolve(m_buildSeq, 1, "Build failed");
}

void MainWindow::launchApp(const QString &seq, const QJsonArray &args) {
    if (args.size() < 1) {
        resolve(seq, 1, "Missing launch path");
        return;
    }
    QString path = args[0].toString();

    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                QFile::ReadUser  | QFile::ExeUser  | QFile::ReadGroup | QFile::ExeGroup);

    logToConsole("Launching process detached: " + path, "system");

    if (QProcess::startDetached(path)) {
        resolve(seq, 0, "Launched successfully");
    } else {
        
        logToConsole("Direct execution failed. Running fallback Way 2: Launching via shell process...", "warning");
        if (QProcess::startDetached("/bin/sh", {path})) {
            resolve(seq, 0, "Launched successfully via shell");
        } else {
            resolve(seq, 1, "All launch ways failed.");
        }
    }
}

void MainWindow::uninstallApp(const QString &seq, const QJsonArray &args) {
    if (args.size() < 2) {
        resolve(seq, 1, "Invalid uninstall parameters");
        return;
    }
    QString repoName = args[0].toString();
    QString binaryPath = args[1].toString();

    logToConsole("Terminating running instances of " + repoName + "...", "system");

    QProcess::execute("pkill", {"-9", "-f", repoName});
    
    if (!binaryPath.isEmpty()) {
        QFileInfo info(binaryPath);
        QProcess::execute("pkill", {"-9", "-f", info.fileName()});
        QProcess::execute("killall", {"-9", info.fileName()});
    }

    QProcess pgrep;
    pgrep.start("pgrep", {"-f", repoName});
    pgrep.waitForFinished();
    QString pids = QString::fromUtf8(pgrep.readAllStandardOutput()).trimmed();
    if (!pids.isEmpty()) {
        QStringList pidList = pids.split('\n', Qt::SkipEmptyParts);
        for (const QString &pid : pidList) {
            QProcess::execute("kill", {"-9", pid});
        }
    }

    logToConsole("Removing binaries and source caches...", "system");

    QString appsDir = getLauncherDir() + "/apps/" + repoName;
    QString sourcesDir = getLauncherDir() + "/sources/" + repoName;

    QDir(appsDir).removeRecursively();
    QDir(sourcesDir).removeRecursively();

    resolve(seq, 0, "Successfully uninstalled completely.");
}

void MainWindow::resetLauncherCache(const QString &seq) {
    logToConsole("Resetting launcher application database and directory caches...", "system");

    QDir(getLauncherDir() + "/apps").removeRecursively();
    QDir(getLauncherDir() + "/sources").removeRecursively();
    QFile::remove(getLauncherDir() + "/installed.txt");
    QFile::remove(getLauncherDir() + "/cached_registry.txt");

    resolve(seq, 0, "Launcher cache and applications database reset successfully.");
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    QWindow *win = windowHandle();
    if (win) {
        win->setProperty("_KDE_NET_WM_BLUR_BEHIND_REGION", 1);
        win->setProperty("KDE_NET_WM_BLUR_BEHIND_REGION", 1);
    }
}
