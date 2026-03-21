#include "ChatPage.h"
#include "../app/ApiClient.h"
#include "../util/Formatters.h"
#include "../widgets/GlowCard.h"
#include <memory>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

ChatPage::ChatPage(ApiClient* api, QWidget* parent)
    : BasePage(api, parent)
{
    auto* page = new QWidget;
    auto* outerLay = new QVBoxLayout(page);
    outerLay->setContentsMargins(24, 20, 24, 20);
    outerLay->setSpacing(14);

    // Header
    auto* title = new QLabel(QStringLiteral("Inference Chat"));
    title->setObjectName(QStringLiteral("pageTitle"));
    outerLay->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral(
        "Probe the latest checkpoint with reusable prompt starters, "
        "generation controls, and a transcript view."));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    subtitle->setWordWrap(true);
    outerLay->addWidget(subtitle);

    // Main two-column layout
    auto* columns = new QHBoxLayout;
    columns->setSpacing(18);

    // --- LEFT column: conversation ---
    auto* leftCol = new QVBoxLayout;
    leftCol->setSpacing(12);

    // Status chips
    auto* chipRow = new QHBoxLayout;
    chipRow->setSpacing(8);
    m_chatCheckpointChip = new QLabel(QStringLiteral("Checkpoint: checking"));
    m_chatCheckpointChip->setObjectName(QStringLiteral("dimText"));
    chipRow->addWidget(m_chatCheckpointChip);
    m_chatTrainingChip = new QLabel(QStringLiteral("Training: idle"));
    m_chatTrainingChip->setObjectName(QStringLiteral("dimText"));
    chipRow->addWidget(m_chatTrainingChip);
    chipRow->addStretch(1);
    leftCol->addLayout(chipRow);

    // Prompt presets
    auto* presetRow = new QHBoxLayout;
    presetRow->setSpacing(8);
    auto addPreset = [this, presetRow](const QString& label, const QString& prompt) {
        auto* btn = new QPushButton(label);
        btn->setObjectName(QStringLiteral("actionBtn"));
        btn->setMinimumHeight(34);
        connect(btn, &QPushButton::clicked, this, [this, prompt]() {
            appendPreset(prompt);
        });
        presetRow->addWidget(btn);
    };
    addPreset(QStringLiteral("Model Status"),
              QStringLiteral("Explain what this model is currently optimized for "
                             "and where it is still weak."));
    addPreset(QStringLiteral("Reasoning"),
              QStringLiteral("Solve this step by step and verify the result at the end: "));
    addPreset(QStringLiteral("Code"),
              QStringLiteral("Write a Python function for the following task and "
                             "explain the edge cases: "));
    addPreset(QStringLiteral("Instruction"),
              QStringLiteral("Answer clearly and concisely, then give a short bullet "
                             "list of key points for: "));
    presetRow->addStretch(1);
    leftCol->addLayout(presetRow);

    // Messages area
    m_chatMessages = new QTextEdit;
    m_chatMessages->setReadOnly(true);
    m_chatMessages->setObjectName(QStringLiteral("chatMessages"));
    m_chatMessages->setMinimumHeight(360);
    m_chatMessages->setHtml(QStringLiteral(
        "<p style='color:#7c92ac;font-size:13px;'>No conversation yet. "
        "Use a preset or type your own prompt to test the current checkpoint.</p>"));
    leftCol->addWidget(m_chatMessages, 1);

    // Input row
    m_chatInput = new QLineEdit;
    m_chatInput->setObjectName(QStringLiteral("chatInput"));
    m_chatInput->setPlaceholderText(
        QStringLiteral("Ask the current checkpoint something useful..."));
    m_chatInput->setMinimumHeight(42);
    connect(m_chatInput, &QLineEdit::returnPressed, this, &ChatPage::sendMessage);
    leftCol->addWidget(m_chatInput);

    // Action row
    auto* actionRow = new QHBoxLayout;
    actionRow->setSpacing(10);
    auto* hint = new QLabel(QStringLiteral("Enter to send."));
    hint->setObjectName(QStringLiteral("dimText"));
    actionRow->addWidget(hint);
    actionRow->addStretch(1);

    auto* clearBtn = new QPushButton(QStringLiteral("Clear Chat"));
    clearBtn->setObjectName(QStringLiteral("actionBtn"));
    clearBtn->setMinimumHeight(36);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        if (m_chatMessages) m_chatMessages->clear();
        m_chatTurns = 0;
        if (m_chatTurnCount) m_chatTurnCount->setText(QStringLiteral("0"));
    });
    actionRow->addWidget(clearBtn);

    auto* sendBtn = new QPushButton(QStringLiteral("Send"));
    sendBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
    sendBtn->setMinimumHeight(36);
    connect(sendBtn, &QPushButton::clicked, this, &ChatPage::sendMessage);
    actionRow->addWidget(sendBtn);
    leftCol->addLayout(actionRow);

    columns->addLayout(leftCol, 1);

    // --- RIGHT column: controls sidebar ---
    auto* rightCol = new QVBoxLayout;
    rightCol->setSpacing(14);

    // Session info card
    {
        auto* card = new GlowCard(QStringLiteral("INFERENCE SESSION"));
        card->setFixedWidth(300);
        auto* infoLay = card->contentLayout();
        infoLay->setSpacing(8);

        auto addStat = [infoLay](const QString& label, QLabel*& valueLabel) {
            auto* row = new QHBoxLayout;
            auto* k = new QLabel(label);
            k->setObjectName(QStringLiteral("dimText"));
            row->addWidget(k);
            row->addStretch(1);
            valueLabel = new QLabel(QStringLiteral("--"));
            valueLabel->setObjectName(QStringLiteral("summaryChipValue"));
            row->addWidget(valueLabel);
            infoLay->addLayout(row);
        };
        addStat(QStringLiteral("Turns"), m_chatTurnCount);
        addStat(QStringLiteral("Send State"), m_chatSendState);

        rightCol->addWidget(card);
    }

    // Generation controls card
    {
        auto* card = new GlowCard(QStringLiteral("GENERATION CONTROLS"));
        card->setFixedWidth(300);
        auto* cLay = card->contentLayout();
        cLay->setSpacing(8);

        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(8);

        auto addControl = [grid](const QString& label, QLineEdit*& edit,
                                 const QString& value, int row, int col) {
            auto* lbl = new QLabel(label);
            lbl->setObjectName(QStringLiteral("settingsLabel"));
            grid->addWidget(lbl, row * 2, col);
            edit = new QLineEdit(value);
            edit->setMinimumHeight(34);
            grid->addWidget(edit, row * 2 + 1, col);
        };

        addControl(QStringLiteral("Temperature"), m_chatTemperature,
                   QStringLiteral("0.7"), 0, 0);
        addControl(QStringLiteral("Max Tokens"), m_chatMaxTokens,
                   QStringLiteral("256"), 0, 1);
        addControl(QStringLiteral("Top-k"), m_chatTopK,
                   QStringLiteral("50"), 1, 0);
        addControl(QStringLiteral("Top-p"), m_chatTopP,
                   QStringLiteral("0.9"), 1, 1);

        cLay->addLayout(grid);

        auto* modeLbl = new QLabel(QStringLiteral("Prompt Mode"));
        modeLbl->setObjectName(QStringLiteral("settingsLabel"));
        cLay->addWidget(modeLbl);

        m_chatMode = new QComboBox;
        m_chatMode->addItems({
            QStringLiteral("Balanced"),
            QStringLiteral("Reasoning"),
            QStringLiteral("Coding"),
            QStringLiteral("Strict concise"),
        });
        m_chatMode->setMinimumHeight(34);
        cLay->addWidget(m_chatMode);

        auto* checkpointLbl = new QLabel(QStringLiteral("Checkpoint Target"));
        checkpointLbl->setObjectName(QStringLiteral("settingsLabel"));
        cLay->addWidget(checkpointLbl);

        m_chatCheckpointSelect = new QComboBox;
        m_chatCheckpointSelect->setMinimumHeight(34);
        m_chatCheckpointSelect->addItem(QStringLiteral("Latest available checkpoint"), QString());
        connect(m_chatCheckpointSelect, &QComboBox::currentIndexChanged, this, [this]() {
            if (!m_chatCheckpointSelect) {
                return;
            }
            m_selectedCheckpointPath = m_chatCheckpointSelect->currentData().toString();
            if (m_chatCheckpointChip) {
                const QString currentLabel = m_chatCheckpointSelect->currentText();
                m_chatCheckpointChip->setText(QStringLiteral("Checkpoint: %1").arg(currentLabel.isEmpty()
                    ? QStringLiteral("checking")
                    : currentLabel));
            }
        });
        cLay->addWidget(m_chatCheckpointSelect);

        rightCol->addWidget(card);
    }

    // Quick evaluations card
    {
        auto* card = new GlowCard(QStringLiteral("QUICK EVALUATIONS"));
        card->setFixedWidth(300);
        auto* qLay = card->contentLayout();
        qLay->setSpacing(8);

        auto addGuide = [this, qLay](const QString& guideTitle,
                                      const QString& desc,
                                      const QString& prompt) {
            auto* item = new QWidget;
            item->setObjectName(QStringLiteral("card"));
            item->setAttribute(Qt::WA_StyledBackground, true);
            auto* iLay = new QVBoxLayout(item);
            iLay->setContentsMargins(12, 10, 12, 10);
            iLay->setSpacing(4);

            auto* t = new QLabel(guideTitle);
            t->setObjectName(QStringLiteral("boldText"));
            t->setStyleSheet(QStringLiteral("font-size: 12px;"));
            iLay->addWidget(t);

            auto* d = new QLabel(desc);
            d->setObjectName(QStringLiteral("dimText"));
            d->setWordWrap(true);
            iLay->addWidget(d);

            auto* btn = new QPushButton(QStringLiteral("Use"));
            btn->setObjectName(QStringLiteral("actionBtn"));
            btn->setFixedHeight(28);
            btn->setFixedWidth(60);
            connect(btn, &QPushButton::clicked, this, [this, prompt]() {
                appendPreset(prompt);
            });
            iLay->addWidget(btn, 0, Qt::AlignRight);

            qLay->addWidget(item);
        };

        addGuide(
            QStringLiteral("Reasoning Stress Test"),
            QStringLiteral("Check planning depth and verification on multi-step problems."),
            QStringLiteral("Solve this carefully, show the critical reasoning steps, "
                           "then verify the final answer: "));
        addGuide(
            QStringLiteral("Code Synthesis"),
            QStringLiteral("Test code quality, structure, and edge-case awareness."),
            QStringLiteral("Implement a clean solution for this programming task, "
                           "then explain complexity and edge cases: "));
        addGuide(
            QStringLiteral("Instruction Adherence"),
            QStringLiteral("Measure format compliance without drift or filler."),
            QStringLiteral("Follow these instructions exactly, keep the format strict, "
                           "and do not add extra commentary: "));

        rightCol->addWidget(card);
    }

    rightCol->addStretch(1);
    columns->addLayout(rightCol, 0);

    outerLay->addLayout(columns, 1);

    auto* wrapper = buildScrollWrapper(page);
    auto* topLay = new QVBoxLayout(this);
    topLay->setContentsMargins(0, 0, 0, 0);
    topLay->addWidget(wrapper);
}

void ChatPage::appendPreset(const QString& prompt)
{
    if (m_chatInput) {
        m_chatInput->setText(prompt);
        m_chatInput->setFocus();
    }
}

void ChatPage::sendMessage()
{
    QString prompt = m_chatInput ? m_chatInput->text().trimmed() : QString();
    if (prompt.isEmpty()) return;

    // Show user message
    m_chatMessages->append(QStringLiteral(
        "<div style='margin:8px 0;padding:10px 12px;"
        "background:rgba(22,101,52,0.22);"
        "border:1px solid rgba(34,197,94,0.4);border-radius:10px;'>"
        "<div style='font-size:10px;color:#8da3bd;font-weight:700;"
        "letter-spacing:1px;margin-bottom:6px;'>YOU</div>"
        "<div style='color:#e2e8f0;font-size:13px;line-height:1.6;'>%1</div></div>")
        .arg(prompt.toHtmlEscaped()));

    m_chatInput->clear();
    ++m_chatTurns;
    if (m_chatTurnCount) m_chatTurnCount->setText(QString::number(m_chatTurns));
    if (m_chatSendState) m_chatSendState->setText(QStringLiteral("Sending..."));

    // Build payload
    QJsonObject body;
    body[QStringLiteral("prompt")] = prompt;
    if (m_chatTemperature)
        body[QStringLiteral("temperature")] = m_chatTemperature->text().toDouble();
    if (m_chatMaxTokens)
        body[QStringLiteral("max_new_tokens")] = m_chatMaxTokens->text().toInt();
    if (m_chatTopK)
        body[QStringLiteral("top_k")] = m_chatTopK->text().toInt();
    if (m_chatTopP)
        body[QStringLiteral("top_p")] = m_chatTopP->text().toDouble();
    if (m_chatCheckpointSelect) {
        const QString checkpointPath = m_chatCheckpointSelect->currentData().toString().trimmed();
        if (!checkpointPath.isEmpty()) {
            body[QStringLiteral("checkpoint_path")] = checkpointPath;
        }
    }

    m_api->postGenerate(body);

    // Connect to response (one-shot)
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_api, &ApiClient::generateDone, this,
        [this, conn](const QJsonObject& data) {
            disconnect(*conn);

            QString gen = data[QStringLiteral("generated")].toString();
            QString err = data[QStringLiteral("error")].toString();

            if (!err.isEmpty()) {
                m_chatMessages->append(QStringLiteral(
                    "<div style='margin:8px 0;padding:10px 12px;"
                    "background:rgba(127,29,29,0.16);"
                    "border:1px solid rgba(239,68,68,0.45);border-radius:10px;'>"
                    "<div style='font-size:10px;color:#ff667d;font-weight:700;"
                    "letter-spacing:1px;margin-bottom:6px;'>ERROR</div>"
                    "<div style='color:#ff8383;font-size:13px;'>%1</div></div>")
                    .arg(err.toHtmlEscaped()));
            } else {
                m_chatMessages->append(QStringLiteral(
                    "<div style='margin:8px 0;padding:10px 12px;"
                    "background:rgba(11,19,31,0.97);"
                    "border:1px solid #16273b;border-radius:10px;'>"
                    "<div style='font-size:10px;color:#8da3bd;font-weight:700;"
                    "letter-spacing:1px;margin-bottom:6px;'>AI</div>"
                    "<div style='color:#e2e8f0;font-size:13px;line-height:1.6;"
                    "white-space:pre-wrap;'>%1</div></div>")
                    .arg(gen.toHtmlEscaped()));
            }

            if (m_chatSendState)
                m_chatSendState->setText(QStringLiteral("Ready"));
        });
}

void ChatPage::updateFromState(const QJsonObject& state)
{
    const auto jobs = state[QStringLiteral("jobs")].toObject();
    const auto processes = state[QStringLiteral("processes")].toObject();
    const auto training = jobs[QStringLiteral("training")].toObject();
    const QJsonObject checkpoint = state[QStringLiteral("checkpoint")].toObject();

    refreshCheckpointSelector(checkpoint);

    if (m_chatCheckpointChip) {
        const bool hasCheckpoint = checkpoint[QStringLiteral("available")].toBool(false);
        QString checkpointLabel = hasCheckpoint
            ? checkpoint.value(QStringLiteral("name")).toString(QStringLiteral("Latest available checkpoint"))
            : QStringLiteral("none");
        if (m_chatCheckpointSelect && m_chatCheckpointSelect->count() > 0) {
            checkpointLabel = m_chatCheckpointSelect->currentText();
        }
        m_chatCheckpointChip->setText(QStringLiteral("Checkpoint: %1").arg(checkpointLabel));
    }

    if (m_chatTrainingChip) {
        const bool trainingRunning =
            processes[QStringLiteral("training")].toObject()
                [QStringLiteral("running")].toBool();
        m_chatTrainingChip->setText(trainingRunning
            ? QStringLiteral("Training: running")
            : QStringLiteral("Training: idle"));
    }

    if (m_chatSendState && m_chatSendState->text() == QStringLiteral("--")) {
        m_chatSendState->setText(QStringLiteral("Ready"));
    }
}

void ChatPage::refreshCheckpointSelector(const QJsonObject& checkpoint)
{
    if (!m_chatCheckpointSelect) {
        return;
    }

    const QString desiredPath = m_selectedCheckpointPath;
    const QString latestPath = checkpoint.value(QStringLiteral("path")).toString();
    const QString latestName = checkpoint.value(QStringLiteral("name")).toString(QStringLiteral("Latest available checkpoint"));
    const QJsonArray entries = checkpoint.value(QStringLiteral("entries")).toArray();
    const bool hasCheckpoint = checkpoint.value(QStringLiteral("available")).toBool(false);

    QSignalBlocker blocker(m_chatCheckpointSelect);
    m_chatCheckpointSelect->clear();

    if (!hasCheckpoint) {
        m_chatCheckpointSelect->addItem(QStringLiteral("No checkpoints found"), QString());
        m_chatCheckpointSelect->setEnabled(false);
        m_selectedCheckpointPath.clear();
        return;
    }

    m_chatCheckpointSelect->setEnabled(true);
    m_chatCheckpointSelect->addItem(
        QStringLiteral("Latest available (%1)").arg(latestName.isEmpty() ? QStringLiteral("checkpoint") : latestName),
        QString());

    int selectedIndex = 0;
    int row = 1;
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const QString path = entry.value(QStringLiteral("path")).toString();
        if (name.isEmpty() || path.isEmpty()) {
            continue;
        }
        m_chatCheckpointSelect->addItem(name, path);
        if (!desiredPath.isEmpty() && path == desiredPath) {
            selectedIndex = row;
        }
        ++row;
    }

    if (desiredPath.isEmpty() || (selectedIndex == 0 && !desiredPath.isEmpty() && desiredPath != latestPath)) {
        m_selectedCheckpointPath.clear();
    }

    m_chatCheckpointSelect->setCurrentIndex(selectedIndex);
}
