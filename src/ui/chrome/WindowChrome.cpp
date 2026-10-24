#include "WindowChrome.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QWindow>

// Chromeless is macOS-only: Windows keeps its native frame (see the header).
WindowChrome::WindowChrome(bool chromeless, QObject* parent)
#ifdef Q_OS_MACOS
    : QObject(parent), chromeless_(chromeless) {}
#else
    : QObject(parent), chromeless_(false) { (void)chromeless; }
#endif

bool WindowChrome::nativeLights() const {
#ifdef Q_OS_MACOS
    return chromeless_;
#else
    return false;
#endif
}

void WindowChrome::attach(QWindow* w) {
    if (!w) return;
    // Headless runs (offscreen platform) have no native window ids.
    const QString platform = QGuiApplication::platformName();
    window_ = w;
    nativeWindow_ = platform == QLatin1String("cocoa");
    // The platform window comes and goes under the QWindow: QWindow
    // destroys it on an accepted close (QWindow::event, QEvent::Close →
    // destroy()) and creates a new one on the next show(). Every creation
    // ends with a SurfaceCreated event — the one hook that fires AFTER the
    // native window exists (visibleChanged fires before create()).
    w->installEventFilter(this);
    applyNative();
}

bool WindowChrome::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == window_ && ev->type() == QEvent::PlatformSurface
        && static_cast<QPlatformSurfaceEvent*>(ev)->surfaceEventType()
               == QPlatformSurfaceEvent::SurfaceCreated) {
        applyNative();
    }
    return QObject::eventFilter(obj, ev);
}

void WindowChrome::applyNative() {
    // Not before the platform window exists (a --background launch keeps
    // the window hidden and uncreated past attach(); SurfaceCreated
    // catches it later) — attachMac's winId() would force the creation.
    if (!window_ || !window_->handle()) return;
#ifdef Q_OS_MACOS
    if (chromeless_ && nativeWindow_) attachMac(window_);
#endif
}

bool WindowChrome::startMove(QWindow* w) {
    if (!chromeless_ || !w) return false;
    if (w->visibility() == QWindow::FullScreen) return false;
    return w->startSystemMove();
}

void WindowChrome::toggleZoom(QWindow* w) {
    if (!w) return;
    if (w->visibility() == QWindow::FullScreen) return;
    if (w->windowStates().testFlag(Qt::WindowMaximized)) w->showNormal();
    else w->showMaximized();
}
