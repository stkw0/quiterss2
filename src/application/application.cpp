/****************************************************************************
**
** QuiteRSS is a open-source cross-platform news feed reader
** Copyright (C) 2011-2017 QuiteRSS Team <quiterssteam@gmail.com>
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <https://www.gnu.org/licenses/>.
**
****************************************************************************/
#include "application.h"
#include "systemtray.h"
#include "webengine.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QQmlFileSelector>
#include <QQmlContext>
#include <QStandardPaths>
#include <QTranslator>
#include <QTimer>

Application::Application(int &argc, char **argv) :
    QtSingleApplication(argc, argv),
    m_isClosing(false),
    m_noDebugOutput(false),
    m_translator(0)
{
    QString message = arguments().value(1);
    if (isRunning()) {
        if (argc == 1) {
            sendMessage("--show");
        } else {
            for (int i = 2; i < argc; ++i)
                message += '\n' + arguments().value(i);
            sendMessage(message);
        }
        setClosing();
        return;
    } else {
        if (message.contains("--exit", Qt::CaseInsensitive)) {
            setClosing();
            return;
        }
    }

    setApplicationName("QuiteRSS");
    setOrganizationName("QuiteRSS");
    setOrganizationDomain("quiterss.org");
    setApplicationVersion(APP_VERSION);
    setWindowIcon(QIcon(":/images/quiterss.png"));
    setQuitOnLastWindowClosed(false);

    setAttribute(Qt::AA_UseHighDpiPixmaps);
    setAttribute(Qt::AA_EnableHighDpiScaling);

    checkPortable();
    checkDir();
    createSettings();

    qWarning() << "Run application";

    initTranslator();
    createGoogleAnalytics();
    createSystemTray();
    WebEngine::initialize();

    QQmlFileSelector *qfs = new QQmlFileSelector(&m_engine, &m_engine);
    QStringList selectors = WebEngine::getQmlSelectors();
    qfs->setExtraSelectors(selectors);

    m_engine.rootContext()->setContextProperty("mainApp", this);
    m_engine.load(QUrl(QStringLiteral("qrc:/qml/mainwindow.qml")));
    // Loading slow components
    emit setSplashScreenValue(7);
    QTimer::singleShot(2000, this, &Application::showWindow);

    receiveMessage(message);
    connect(this, &Application::messageReceived,
            this, &Application::receiveMessage);
}

Application::~Application()
{

}

Application *Application::getInstance()
{
    return static_cast<Application *>(QCoreApplication::instance());
}

void Application::quitApp()
{
    if (m_analytics) {
        m_analytics->endSession();
        m_analytics->waitForIdle();
        delete m_analytics;
    }

    qWarning() << "End application";
    quit();
}

void Application::commitData(QSessionManager &manager)
{
    manager.release();
    quitApp();
}

void Application::receiveMessage(const QString &message)
{
    if (!message.isEmpty()) {
        qWarning() << QString("Received message: %1").arg(message);

        QStringList params = message.split('\n');
        foreach (QString param, params) {
            if (param == "--show") {
                if (isClosing())
                    return;
                emit showWindow();
            }
            if (param == "--exit")
                emit closeWindow();
            if (param.contains("feed:", Qt::CaseInsensitive)) {
                QClipboard *clipboard = QApplication::clipboard();
                if (param.contains("https://", Qt::CaseInsensitive)) {
                    param.remove(0, 5);
                    clipboard->setText(param);
                } else {
                    param.remove(0, 7);
                    clipboard->setText("http://" + param);
                }
                emit addFeed();
            }
        }
    }
}

void Application::checkPortable()
{
#if defined(Q_OS_WIN)
    m_isPortable = true;
    QString fileName(QCoreApplication::applicationDirPath() + "/portable.dat");
    if (!QFile::exists(fileName)) {
        m_isPortable = false;
    }
    if (m_isPortable) {
        fileName = QCoreApplication::applicationDirPath() + "/../../QuiteRSSPortable.exe";
        if (QFile::exists(fileName)) {
            m_isPortableAppsCom = true;
            QFile::remove(QCoreApplication::applicationDirPath() + "/updater.exe");
        }
    }
#endif
}

void Application::checkDir()
{
#if defined(Q_OS_WIN) || defined(Q_OS_OS2)
    m_resourcesDir = QCoreApplication::applicationDirPath();
#else
#if defined(Q_OS_MAC)
    m_resourcesDir = QCoreApplication::applicationDirPath() + "/../Resources";
#elif defined(Q_OS_ANDROID)
    m_resourcesDir = ":";
#else
    m_resourcesDir = RESOURCES_DIR;
#endif
#endif

    if (isPortable()) {
        m_dataDir = QCoreApplication::applicationDirPath() + "/data";
        m_cacheDir = m_dataDir + "/cache";
        m_soundDir = m_resourcesDir + "/sound";
    } else {
        m_dataDir = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
        m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        m_soundDir = m_resourcesDir + "/sound";
    }
    QDir dir(m_dataDir);
    dir.mkpath(m_dataDir);
}

void Application::createSettings()
{
    QString fileName;
    if (isPortable())
        fileName = dataDir() + "/" + QCoreApplication::applicationName() + ".ini";
    Settings::createSettings(fileName);

    Settings settings;
    settings.beginGroup("Main");
    m_storeDBMemory = settings.value("storeDBMemory", true).toBool();
    m_isSaveDataLastFeed = settings.value("createLastFeed", false).toBool();
    m_showSplashScreen = settings.value("showSplashScreen", true).toBool();
    m_updateFeedsStartUp = settings.value("autoUpdatefeedsStartUp", false).toBool();
    m_noDebugOutput = settings.value("noDebugOutput", true).toBool();

    QString lang;
    QString localLang = QLocale::system().name();
    bool findLang = false;
    QDir langDir(resourcesDir() + "/translations");
    foreach (QString file, langDir.entryList(QStringList("*.qm"), QDir::Files)) {
        lang = file.section('.', 0, 0).section('_', 1);
        if (localLang == lang) {
            lang = localLang;
            findLang = true;
            break;
        }
    }
    if (!findLang) {
        localLang = localLang.left(2);
        foreach (QString file, langDir.entryList(QStringList("*.qm"), QDir::Files)) {
            lang = file.section('.', 0, 0).section('_', 1);
            if (localLang.contains(lang, Qt::CaseInsensitive)) {
                lang = localLang;
                findLang = true;
                break;
            }
        }
    }
    if (!findLang)
        lang = "en";
    m_langFileName = settings.value("langFileName", lang).toString();

    settings.endGroup();
}

void Application::initTranslator()
{
    if (!m_translator)
        m_translator = new QTranslator(this);
    removeTranslator(m_translator);
    m_translator->load(resourcesDir() + QString("/translations/quiterss_%1").arg(m_langFileName));
    installTranslator(m_translator);
}

void Application::createGoogleAnalytics()
{
    Settings settings;
    bool statisticsEnabled = settings.value("Main/statisticsEnabled", true).toBool();
    if (statisticsEnabled) {
        QString clientID;
        if (!settings.contains("GAnalytics-cid")) {
            settings.setValue("GAnalytics-cid", QUuid::createUuid().toString());
        }
        clientID = settings.value("GAnalytics-cid").toString();
        m_analytics = new GAnalytics(this, TRACKING_ID, clientID);
        m_engine.rootContext()->setContextProperty("analytics", m_analytics);
        m_analytics->generateUserAgentEtc();
        m_analytics->startSession();
    }
}

void Application::createSystemTray()
{
    m_systemTray = new SystemTray(this);
    m_systemTray->retranslateStrings();
    m_engine.rootContext()->setContextProperty("systemTray", m_systemTray);
    connect(m_systemTray, &SystemTray::signalQuit,
            this, &Application::quitApp);

    m_systemTray->show();
}
