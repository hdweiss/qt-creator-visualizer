/*
  This file was generated by the Html5 Application wizard of Qt Creator.
  Html5ApplicationViewer is a convenience class containing mobile device specific
  code such as screen orientation handling.
  It is recommended not to modify this file, since newer versions of Qt Creator
  may offer an updated version of it.
*/

#include "html5applicationviewer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsLinearLayout>
#include <QGraphicsWebView>
#include <QWebFrame>

#ifdef TOUCH_OPTIMIZED_NAVIGATION
#include <QTimer>
#include <QGraphicsSceneMouseEvent>
#include <QWebElement>
#include "navigationcontroller.h"
#include "webtouchevent.h"
#include "webtouchnavigation.h"
#include "webtouchphysics.h"
#include "webtouchphysicsinterface.h"
#include "webtouchscroller.h"
#endif // TOUCH_OPTIMIZED_NAVIGATION

class Html5ApplicationViewerPrivate : public QGraphicsView
{
    Q_OBJECT
public:
    Html5ApplicationViewerPrivate(QWidget *parent = 0);

    void resizeEvent(QResizeEvent *event);
    static QString adjustPath(const QString &path);

public slots:
    void quit();

private slots:
    void addToJavaScript();

signals:
    void quitRequested();

public:
    QGraphicsWebView *m_webView;
#ifdef TOUCH_OPTIMIZED_NAVIGATION
    NavigationController *m_controller;
#endif // TOUCH_OPTIMIZED_NAVIGATION
};

Html5ApplicationViewerPrivate::Html5ApplicationViewerPrivate(QWidget *parent)
    : QGraphicsView(parent)
{
    QGraphicsScene *scene = new QGraphicsScene;
    setScene(scene);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_webView = new QGraphicsWebView;
    m_webView->setAcceptTouchEvents(true);
    m_webView->setAcceptHoverEvents(false);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    scene->addItem(m_webView);
    scene->setActiveWindow(m_webView);
#ifdef TOUCH_OPTIMIZED_NAVIGATION
    m_controller = new NavigationController(parent, m_webView);
#endif // TOUCH_OPTIMIZED_NAVIGATION
    connect(m_webView->page()->mainFrame(),
            SIGNAL(javaScriptWindowObjectCleared()), SLOT(addToJavaScript()));
}

void Html5ApplicationViewerPrivate::resizeEvent(QResizeEvent *event)
{
    m_webView->resize(event->size());
}

QString Html5ApplicationViewerPrivate::adjustPath(const QString &path)
{
#ifdef Q_OS_UNIX
#ifdef Q_OS_MAC
    if (!QDir::isAbsolutePath(path))
        return QCoreApplication::applicationDirPath()
                + QLatin1String("/../Resources/") + path;
#else
    const QString pathInInstallDir = QCoreApplication::applicationDirPath()
        + QLatin1String("/../") + path;
    if (pathInInstallDir.contains(QLatin1String("opt"))
            && pathInInstallDir.contains(QLatin1String("bin"))
            && QFileInfo(pathInInstallDir).exists()) {
        return pathInInstallDir;
    }
#endif
#endif
    return path;
}

void Html5ApplicationViewerPrivate::quit()
{
    emit quitRequested();
}

void Html5ApplicationViewerPrivate::addToJavaScript()
{
    m_webView->page()->mainFrame()->addToJavaScriptWindowObject("Qt", this);
}

Html5ApplicationViewer::Html5ApplicationViewer(QWidget *parent)
    : QWidget(parent)
    , m_d(new Html5ApplicationViewerPrivate(this))
{
    connect(m_d, SIGNAL(quitRequested()), SLOT(close()));
    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(m_d);
    layout->setMargin(0);
    setLayout(layout);
}

Html5ApplicationViewer::~Html5ApplicationViewer()
{
    delete m_d;
}

void Html5ApplicationViewer::loadFile(const QString &fileName)
{
    m_d->m_webView->setUrl(QUrl(Html5ApplicationViewerPrivate::adjustPath(fileName)));
}

void Html5ApplicationViewer::loadUrl(const QUrl &url)
{
    m_d->m_webView->setUrl(url);
}

void Html5ApplicationViewer::setOrientation(ScreenOrientation orientation)
{
#if defined(Q_OS_SYMBIAN)
    // If the version of Qt on the device is < 4.7.2, that attribute won't work
    if (orientation != ScreenOrientationAuto) {
        const QStringList v = QString::fromLatin1(qVersion()).split(QLatin1Char('.'));
        if (v.count() == 3 && (v.at(0).toInt() << 16 | v.at(1).toInt() << 8 | v.at(2).toInt()) < 0x040702) {
            qWarning("Screen orientation locking only supported with Qt 4.7.2 and above");
            return;
        }
    }
#endif // Q_OS_SYMBIAN

    Qt::WidgetAttribute attribute;
    switch (orientation) {
#if QT_VERSION < 0x040702
    // Qt < 4.7.2 does not yet have the Qt::WA_*Orientation attributes
    case ScreenOrientationLockPortrait:
        attribute = static_cast<Qt::WidgetAttribute>(128);
        break;
    case ScreenOrientationLockLandscape:
        attribute = static_cast<Qt::WidgetAttribute>(129);
        break;
    default:
    case ScreenOrientationAuto:
        attribute = static_cast<Qt::WidgetAttribute>(130);
        break;
#else // QT_VERSION < 0x040702
    case ScreenOrientationLockPortrait:
        attribute = Qt::WA_LockPortraitOrientation;
        break;
    case ScreenOrientationLockLandscape:
        attribute = Qt::WA_LockLandscapeOrientation;
        break;
    default:
    case ScreenOrientationAuto:
        attribute = Qt::WA_AutoOrientation;
        break;
#endif // QT_VERSION < 0x040702
    };
    setAttribute(attribute, true);
}

void Html5ApplicationViewer::showExpanded()
{
#if defined(Q_OS_SYMBIAN) || defined(Q_WS_SIMULATOR)
    showFullScreen();
#elif defined(Q_WS_MAEMO_5)
    showMaximized();
#else
    show();
#endif
}

QGraphicsWebView *Html5ApplicationViewer::webView() const
{
    return m_d->m_webView;
}

#include "html5applicationviewer.moc"
