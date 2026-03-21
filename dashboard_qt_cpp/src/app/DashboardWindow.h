#ifndef APP_DASHBOARD_WINDOW_H
#define APP_DASHBOARD_WINDOW_H

#include <QMainWindow>
#include <QPoint>
#include <QVector>

class ApiClient;
class DataPage;
class DiagnosticsPage;
class JobsPage;
class LiveFeedPage;
class MetricsPage;
class OverviewPage;
class ReportsPage;
class SettingsPage;
class ChatPage;
class PillBadge;
class StatusIndicator;
class QComboBox;
class QButtonGroup;
class QJsonObject;
class QLabel;
class QPropertyAnimation;
class QPushButton;
class QResizeEvent;
class QStackedWidget;
class QTimer;
class QWidget;
class QVBoxLayout;

class DashboardWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit DashboardWindow(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct NavItem {
        QString icon;
        QString title;
        QString subtitle;
        int pageIndex = 0;
    };

    void buildUi();
    QWidget* buildChromeBar();
    QWidget* buildSidebar();
    QWidget* buildStatusStrip();
    void buildPageStack();
    void addNavSection(QVBoxLayout* layout, const QString& text);
    void addNavButton(QVBoxLayout* layout, const NavItem& item);
    QWidget* makeQuickAccessRow(const QString& label, PillBadge** badgeOut);
    void registerSignals();
    void refreshState();
    void refreshDiagnostics();
    void handleState(const QJsonObject& state);
    void handleDiagnostics(const QJsonObject& diagnostics);
    void updateSidebarMetrics(const QJsonObject& state);
    void setConnected(bool connected);
    void applyMode(int index);
    void applySidebarCollapsed(bool collapsed);
    void syncMaxButtonGlyph();
    void restoreForTitleBarDrag(const QPoint& globalPos, const QPoint& localPos);
    void toggleMaximized();
    void buildNotificationToast(QWidget* parent);
    void showNotification(const QString& title, const QString& message, const QString& severity = QStringLiteral("info"));
    void positionNotificationToast();

    QString m_baseUrl;
    ApiClient* m_api = nullptr;
    QTimer* m_stateTimer = nullptr;
    QTimer* m_diagnosticsTimer = nullptr;

    QWidget* m_titleBar = nullptr;
    QLabel* m_windowTitle = nullptr;
    QPushButton* m_minButton = nullptr;
    QPushButton* m_maxButton = nullptr;
    QPushButton* m_closeButton = nullptr;

    QWidget* m_sidebar = nullptr;
    QPushButton* m_sidebarToggle = nullptr;
    QWidget* m_brandBadge = nullptr;
    QLabel* m_brandStatusLight = nullptr;
    QLabel* m_brandName = nullptr;
    QLabel* m_brandSubtitle = nullptr;
    QComboBox* m_modeSelect = nullptr;
    QButtonGroup* m_navGroup = nullptr;
    QVector<QPushButton*> m_navButtons;
    QVector<QLabel*> m_sidebarLabels;
    QVector<QWidget*> m_hideWhenCollapsed;

    QLabel* m_gpuVal = nullptr;
    QLabel* m_loadVal = nullptr;
    QLabel* m_ramVal = nullptr;
    PillBadge* m_datasetBadge = nullptr;
    PillBadge* m_systemBadge = nullptr;
    PillBadge* m_logsBadge = nullptr;

    QStackedWidget* m_pages = nullptr;
    OverviewPage* m_overviewPage = nullptr;
    MetricsPage* m_metricsPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    JobsPage* m_jobsPage = nullptr;
    ChatPage* m_chatPage = nullptr;
    ReportsPage* m_reportsPage = nullptr;
    LiveFeedPage* m_liveFeedPage = nullptr;
    DataPage* m_dataPage = nullptr;
    DiagnosticsPage* m_diagnosticsPage = nullptr;

    StatusIndicator* m_statusIndicator = nullptr;
    QLabel* m_statusText = nullptr;
    QLabel* m_statusMeta = nullptr;
    QWidget* m_notificationToast = nullptr;
    QLabel* m_notificationTitle = nullptr;
    QLabel* m_notificationMessage = nullptr;
    QTimer* m_notificationTimer = nullptr;
    QPropertyAnimation* m_notificationOpacity = nullptr;
    double m_lastAlertTs = 0.0;
    QString m_lastNotificationFingerprint;
    qint64 m_lastNotificationAtMs = 0;
    bool m_backendDisconnectNotified = false;
    QString m_lastBackendDisconnectError;

    bool m_sidebarCollapsed = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
};

#endif
