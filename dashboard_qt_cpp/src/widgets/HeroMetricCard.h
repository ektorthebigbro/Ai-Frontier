#ifndef HERO_METRIC_CARD_H
#define HERO_METRIC_CARD_H
#include <QColor>
#include <QLabel>
#include <QWidget>
class IconBadge;

class HeroMetricCard : public QWidget {
    Q_OBJECT
public:
    HeroMetricCard(const QString& icon, const QColor& iconColor, const QString& title, QWidget* parent = nullptr);
    void setValue(const QString& value);
    void setChip(const QString& text, const QColor& chipColor = QColor());
    void setTrend(const QString& text, bool positive = true, const QString& glyph = QString());
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    IconBadge* m_badge;
    QLabel* m_title;
    QLabel* m_value;
    QLabel* m_chip;
    QLabel* m_trend;
    QColor m_iconColor;
};
#endif
