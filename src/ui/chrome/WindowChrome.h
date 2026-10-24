// WindowChrome — the chromeless window (2026-10-23, the minNotes model). The
// native title bar is gone; Main.qml's title row — the tab strip — is the
// title bar: every stretch of it no control claims drags the window, a
// double-click zooms. "As native as possible":
//   macOS   — the window keeps its titled style with Qt's
//             ExpandedClientAreaHint + NoTitleBarBackgroundHint (Main.qml
//             flags): the content extends under a transparent title bar,
//             the NATIVE traffic lights stay (zoom / fullscreen / tiling
//             keep working) and the row insets its content past them.
//             attachMac() hides the title text AppKit still draws.
//   Windows — the native frame and title bar (since 0.5.9). Qt's expanded
//             client area drew its own caption buttons keyed to the OS
//             light/dark setting and Windows 11 (white borders, black
//             glyphs, invisible hovers on light-mode PCs and on Windows
//             10); a farm tool doesn't need the merged row. The title row
//             is an ordinary toolbar there, and main_qt.cpp asks Qt for the
//             dark scheme so the title bar matches.
//   Resizing is the OS's. `--native-frame` (dev) keeps the native frame on
//   macOS too; the row then has no inset.
#pragma once

#include <QObject>

class QWindow;

class WindowChrome : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool chromeless READ chromeless CONSTANT)
    // The native traffic lights sit in the row (macOS).
    Q_PROPERTY(bool nativeLights READ nativeLights CONSTANT)
public:
    explicit WindowChrome(bool chromeless, QObject* parent = nullptr);
    bool chromeless() const { return chromeless_; }
    bool nativeLights() const;
    // Apply what the flags can't once the window exists (macOS: hide the
    // title text). Re-applied
    // every time the NATIVE window is created again: an accepted close
    // (close-to-tray) destroys the NSWindow behind the QWindow, and
    // the one the next show() creates is a stock one — on macOS it drew
    // the title text over the tab strip again (2026-10-23).
    void attach(QWindow* w);
    bool eventFilter(QObject* obj, QEvent* ev) override;
    // The row's gestures: a drag past the threshold hands the press to the
    // OS move loop; a double-click zooms.
    Q_INVOKABLE bool startMove(QWindow* w);
    Q_INVOKABLE void toggleZoom(QWindow* w);
private:
    bool chromeless_ = true;
    QWindow* window_ = nullptr;
    bool nativeWindow_ = false;   // a real cocoa window (not the offscreen platform's)
    void applyNative();           // the native side, once the platform window exists
#ifdef Q_OS_MACOS
    void attachMac(QWindow* w);   // WindowChrome_mac.mm
#endif
};
