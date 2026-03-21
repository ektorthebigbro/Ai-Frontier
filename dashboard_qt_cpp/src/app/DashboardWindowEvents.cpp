#include "DashboardWindow.h"

#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QWindow>
#include <QtMath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace {

bool isChromeControl(QWidget* child, QWidget* minButton, QWidget* maxButton, QWidget* closeButton) {
    return child &&
        (child == minButton ||
         child == maxButton ||
         child == closeButton ||
         qobject_cast<QPushButton*>(child));
}

}  // namespace

bool DashboardWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_titleBar) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            toggleMaximized();
            return true;
        }

        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                if (QWidget* child = m_titleBar->childAt(mouse->position().toPoint())) {
                    if (isChromeControl(child, m_minButton, m_maxButton, m_closeButton)) {
                        return false;
                    }
                }

                const bool restoreForDrag = isMaximized() || isFullScreen();
#ifndef Q_OS_WIN
                if (restoreForDrag) {
                    restoreForTitleBarDrag(mouse->globalPosition().toPoint(), mouse->position().toPoint());
                }
#endif

#ifdef Q_OS_WIN
                if (isFullScreen()) {
                    restoreForTitleBarDrag(mouse->globalPosition().toPoint(), mouse->position().toPoint());
                }
                if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
                    ReleaseCapture();
                    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                    return true;
                }
                if (!restoreForDrag && windowHandle() && windowHandle()->startSystemMove()) {
                    return true;
                }
                m_dragging = true;
                m_dragOffset = mouse->globalPosition().toPoint() - frameGeometry().topLeft();
                m_titleBar->grabMouse();
                return true;
#else
                m_dragging = true;
                m_dragOffset = mouse->globalPosition().toPoint() - frameGeometry().topLeft();
                m_titleBar->grabMouse();
                return true;
#endif
            }
        }

        if (event->type() == QEvent::MouseMove && m_dragging && !isMaximized() && !isFullScreen()) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            move(mouse->globalPosition().toPoint() - m_dragOffset);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            if (m_dragging && QWidget::mouseGrabber() == m_titleBar) {
                m_titleBar->releaseMouse();
            }
            m_dragging = false;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

bool DashboardWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    Q_UNUSED(eventType);
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_EXITSIZEMOVE && !isMaximized() && !isFullScreen()) {
        const QRect frame = frameGeometry();
        QScreen* screen = QGuiApplication::screenAt(frame.center());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen) {
            const QRect available = screen->availableGeometry();
            if (frame.top() <= available.top() + 2 &&
                frame.center().x() >= available.left() &&
                frame.center().x() <= available.right()) {
                showMaximized();
                syncMaxButtonGlyph();
                *result = 0;
                return true;
            }
        }
    }

    if (msg->message == WM_NCHITTEST) {
        const QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
        const QPoint localPos = mapFromGlobal(globalPos);
        const QRect windowRect = rect();
        const int border = (isMaximized() || isFullScreen()) ? 0 : 8;
        if (!isMaximized() && !isFullScreen()) {
            const bool onLeft = localPos.x() >= 0 && localPos.x() < border;
            const bool onRight = localPos.x() < windowRect.width() && localPos.x() >= windowRect.width() - border;
            const bool onTop = localPos.y() >= 0 && localPos.y() < border;
            const bool onBottom = localPos.y() < windowRect.height() && localPos.y() >= windowRect.height() - border;
            if (onTop && onLeft) { *result = HTTOPLEFT; return true; }
            if (onTop && onRight) { *result = HTTOPRIGHT; return true; }
            if (onBottom && onLeft) { *result = HTBOTTOMLEFT; return true; }
            if (onBottom && onRight) { *result = HTBOTTOMRIGHT; return true; }
            if (onLeft) { *result = HTLEFT; return true; }
            if (onRight) { *result = HTRIGHT; return true; }
            if (onTop) { *result = HTTOP; return true; }
            if (onBottom) { *result = HTBOTTOM; return true; }
        }

        if (m_titleBar) {
            const QRect titleRect(m_titleBar->mapTo(this, QPoint(0, 0)), m_titleBar->size());
            if (titleRect.contains(localPos)) {
                if (QWidget* child = childAt(localPos)) {
                    if (isChromeControl(child, m_minButton, m_maxButton, m_closeButton)) {
                        return false;
                    }
                }
                *result = HTCAPTION;
                return true;
            }
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif

    return QMainWindow::nativeEvent(eventType, message, result);
}

void DashboardWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_pages && m_pages->currentWidget()) {
        QWidget* current = m_pages->currentWidget();
        current->move(0, 0);
        current->setGeometry(m_pages->rect());
    }
    positionNotificationToast();
}

void DashboardWindow::syncMaxButtonGlyph() {
    if (!m_maxButton) {
        return;
    }
    m_maxButton->setText((isMaximized() || isFullScreen())
        ? QStringLiteral("\u25A0")
        : QStringLiteral("\u25A1"));
}

void DashboardWindow::restoreForTitleBarDrag(const QPoint& globalPos, const QPoint& localPos) {
    QScreen* screen = QGuiApplication::screenAt(globalPos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    const QRect available = screen ? screen->availableGeometry() : QRect();
    QRect restoreRect = normalGeometry();
    if (!restoreRect.isValid() || restoreRect.width() <= 0 || restoreRect.height() <= 0) {
        const QSize fallbackSize = available.isValid()
            ? QSize(qMax(960, static_cast<int>(available.width() * 0.72)),
                    qMax(640, static_cast<int>(available.height() * 0.78)))
            : QSize(1200, 820);
        restoreRect = QRect(QPoint(0, 0), available.isValid() ? fallbackSize.boundedTo(available.size()) : fallbackSize);
    }

    const int titleBarWidth = m_titleBar ? qMax(1, m_titleBar->width()) : qMax(1, width());
    const double horizontalRatio = qBound(0.0, static_cast<double>(localPos.x()) / static_cast<double>(titleBarWidth), 1.0);
    const int dragYOffset = m_titleBar
        ? qBound(0, localPos.y(), qMax(0, m_titleBar->height() - 1))
        : 0;

    showNormal();
    resize(restoreRect.size());
    syncMaxButtonGlyph();

    const QSize restoredSize = size();
    int targetX = globalPos.x() - qRound(restoredSize.width() * horizontalRatio);
    int targetY = globalPos.y() - dragYOffset;

    if (available.isValid()) {
        targetX = qBound(available.left(), targetX, available.right() - restoredSize.width() + 1);
        targetY = qBound(available.top(), targetY, available.bottom() - restoredSize.height() + 1);
    }

    move(targetX, targetY);
    m_dragOffset = globalPos - frameGeometry().topLeft();
}

void DashboardWindow::toggleMaximized() {
    if (isMaximized() || isFullScreen()) {
        showNormal();
        syncMaxButtonGlyph();
        return;
    }

    showMaximized();
    syncMaxButtonGlyph();
}
