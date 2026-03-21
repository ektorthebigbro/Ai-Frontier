#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QVariantMap>
#include "ApiClient.h"

class AppController : public QObject
{
    Q_OBJECT

    // Connection / status
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString connectionError READ connectionError NOTIFY connectionErrorChanged)

    // Core state (flat values for QML)
    Q_PROPERTY(double trainLoss READ trainLoss NOTIFY stateChanged)
    Q_PROPERTY(double valLoss READ valLoss NOTIFY stateChanged)
    Q_PROPERTY(double accuracy READ accuracy NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(int epoch READ epoch NOTIFY stateChanged)
    Q_PROPERTY(int totalEpochs READ totalEpochs NOTIFY stateChanged)
    Q_PROPERTY(QString currentJob READ currentJob NOTIFY stateChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(double gpuUsage READ gpuUsage NOTIFY stateChanged)
    Q_PROPERTY(double cpuLoad READ cpuLoad NOTIFY stateChanged)
    Q_PROPERTY(double ramUsage READ ramUsage NOTIFY stateChanged)
    Q_PROPERTY(QString uptime READ uptime NOTIFY stateChanged)
    Q_PROPERTY(double learningRate READ learningRate NOTIFY stateChanged)
    Q_PROPERTY(int batchSize READ batchSize NOTIFY stateChanged)
    Q_PROPERTY(QString modelArch READ modelArch NOTIFY stateChanged)

    // Full JSON for pages that need it
    Q_PROPERTY(QVariantMap fullState READ fullState NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY diagnosticsChanged)

    // History arrays for charts
    Q_PROPERTY(QVariantList trainLossHistory READ trainLossHistory NOTIFY stateChanged)
    Q_PROPERTY(QVariantList valLossHistory READ valLossHistory NOTIFY stateChanged)
    Q_PROPERTY(QVariantList accuracyHistory READ accuracyHistory NOTIFY stateChanged)
    Q_PROPERTY(QVariantList progressHistory READ progressHistory NOTIFY stateChanged)
    Q_PROPERTY(QVariantList rewardHistory READ rewardHistory NOTIFY stateChanged)
    Q_PROPERTY(QVariantList gpuUsageHistory READ gpuUsageHistory NOTIFY stateChanged)
    Q_PROPERTY(QVariantList ramUsageHistory READ ramUsageHistory NOTIFY stateChanged)

public:
    explicit AppController(QObject* parent = nullptr);

    bool connected() const { return m_connected; }
    QString connectionError() const { return m_connectionError; }
    double trainLoss() const { return m_trainLoss; }
    double valLoss() const { return m_valLoss; }
    double accuracy() const { return m_accuracy; }
    double progress() const { return m_progress; }
    int epoch() const { return m_epoch; }
    int totalEpochs() const { return m_totalEpochs; }
    QString currentJob() const { return m_currentJob; }
    QString stage() const { return m_stage; }
    QString status() const { return m_status; }
    double gpuUsage() const { return m_gpuUsage; }
    double cpuLoad() const { return m_cpuLoad; }
    double ramUsage() const { return m_ramUsage; }
    QString uptime() const { return m_uptime; }
    double learningRate() const { return m_learningRate; }
    int batchSize() const { return m_batchSize; }
    QString modelArch() const { return m_modelArch; }
    QVariantMap fullState() const { return m_fullState; }
    QVariantMap diagnostics() const { return m_diagnostics; }
    QVariantList trainLossHistory() const { return m_trainLossHistory; }
    QVariantList valLossHistory() const { return m_valLossHistory; }
    QVariantList accuracyHistory() const { return m_accuracyHistory; }
    QVariantList progressHistory() const { return m_progressHistory; }
    QVariantList rewardHistory() const { return m_rewardHistory; }
    QVariantList gpuUsageHistory() const { return m_gpuUsageHistory; }
    QVariantList ramUsageHistory() const { return m_ramUsageHistory; }

    // Invokable actions from QML
    Q_INVOKABLE void sendAction(const QString& action, const QVariantMap& params = {});
    Q_INVOKABLE void postActionPath(const QString& endpoint);
    Q_INVOKABLE void saveConfig(const QVariantMap& config);
    Q_INVOKABLE void saveConfigYaml(const QString& yaml);
    Q_INVOKABLE void sendChatMessage(const QString& message, const QVariantMap& params = {});
    Q_INVOKABLE void runDiagnosticChecks();
    Q_INVOKABLE void selfHeal();
    Q_INVOKABLE void reloadModule(const QString& module);
    Q_INVOKABLE void clearDiagnostics();
    Q_INVOKABLE void clearDiagnosticKey(const QString& key);
    Q_INVOKABLE void fetchDiagnosticIssue(const QString& key);
    Q_INVOKABLE void launchBackend();
    Q_INVOKABLE void generateReport();
    Q_INVOKABLE void openLocalPath(const QString& path);
    Q_INVOKABLE void copyText(const QString& text, const QString& successMessage = QString());

signals:
    void connectedChanged();
    void connectionErrorChanged();
    void stateChanged();
    void diagnosticsChanged();
    void chatResponseReceived(const QString& response);
    void diagnosticIssueLoaded(const QVariantMap& issue, const QString& detailText, bool success);
    void actionCompleted(const QString& action, bool success, const QString& message);
    void toastRequested(const QString& message, const QString& type);

private slots:
    void onStateReceived(const QJsonObject& state);
    void onStateFetchFailed(const QString& error);
    void onDiagnosticsReceived(const QJsonObject& diag);
    void onDiagnosticsFetchFailed(const QString& error);
    void onActionDone(const QJsonObject& response);
    void onGenerateDone(const QJsonObject& response);
    void onConfigSaved(bool ok, const QString& message);
    void pollState();
    void pollDiagnostics();

private:
    void appendHistory(QVariantList& history, double value, int limit = 60);

    ApiClient* m_api;
    QTimer* m_stateTimer;
    QTimer* m_diagTimer;

    bool m_connected = false;
    QString m_connectionError;

    // Track pending action context for toast messages
    QString m_pendingAction;
    bool m_pendingIsChat = false;
    bool m_pendingIsReport = false;
    bool m_stateRequestInFlight = false;
    bool m_diagRequestInFlight = false;

    double m_trainLoss = 0.0;
    double m_valLoss = 0.0;
    double m_accuracy = 0.0;
    double m_progress = 0.0;
    int m_epoch = 0;
    int m_totalEpochs = 0;
    QString m_currentJob;
    QString m_stage;
    QString m_status = "idle";
    double m_gpuUsage = 0.0;
    double m_cpuLoad = 0.0;
    double m_ramUsage = 0.0;
    QString m_uptime;
    double m_learningRate = 0.0;
    int m_batchSize = 0;
    QString m_modelArch;

    QVariantMap m_fullState;
    QVariantMap m_diagnostics;
    QVariantList m_trainLossHistory;
    QVariantList m_valLossHistory;
    QVariantList m_accuracyHistory;
    QVariantList m_progressHistory;
    QVariantList m_rewardHistory;
    QVariantList m_gpuUsageHistory;
    QVariantList m_ramUsageHistory;
};
