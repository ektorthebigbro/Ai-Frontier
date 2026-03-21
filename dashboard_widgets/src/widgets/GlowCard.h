#ifndef GLOW_CARD_H
#define GLOW_CARD_H
#include <QColor>
#include <QEnterEvent>
#include <QVBoxLayout>
#include <QWidget>

class QPropertyAnimation;
class QVariantAnimation;

class GlowCard : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QColor glowColor READ glowColor WRITE setGlowColor)
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
public:
    explicit GlowCard(const QString& title = QString(), QWidget* parent = nullptr);
    QVBoxLayout* contentLayout() const { return m_content; }
    QColor glowColor() const { return m_glowColor; }
    void setGlowColor(const QColor& color);
    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal value);
protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
private:
    QVBoxLayout* m_content = nullptr;
    QColor m_glowColor{30, 60, 90, 80};
    int m_radius = 16;
    QString m_title;
    qreal m_hoverProgress = 0.0;
    qreal m_pulsePhase = 0.0;
    QPropertyAnimation* m_hoverAnimation = nullptr;
    QVariantAnimation* m_pulseAnimation = nullptr;
};
#endif
