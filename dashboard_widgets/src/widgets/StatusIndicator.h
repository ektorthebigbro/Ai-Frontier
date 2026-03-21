#ifndef STATUS_INDICATOR_H
#define STATUS_INDICATOR_H
#include <QColor>
#include <QWidget>

class QVariantAnimation;

class StatusIndicator : public QWidget {
    Q_OBJECT
public:
    explicit StatusIndicator(QWidget* parent = nullptr);
    void setStatus(const QString& text, const QColor& dotColor = QColor(34, 197, 94));
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QString m_text;
    QColor m_dotColor{34, 197, 94};
    qreal m_pulsePhase = 0.0;
    QVariantAnimation* m_pulseAnimation = nullptr;
};
#endif
