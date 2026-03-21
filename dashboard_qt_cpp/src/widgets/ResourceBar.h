#ifndef RESOURCE_BAR_H
#define RESOURCE_BAR_H
#include <QColor>
#include <QWidget>

class QPropertyAnimation;

class ResourceBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal displayedValue READ displayedValue WRITE setDisplayedValue)
public:
    explicit ResourceBar(const QString& label = QString(), QWidget* parent = nullptr);
    void setValue(double fraction, const QString& detail = QString());
    void setBarColor(const QColor& color);
    qreal displayedValue() const { return m_displayedValue; }
    void setDisplayedValue(qreal value) { m_displayedValue = qBound<qreal>(0.0, value, 1.0); update(); }
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QString m_label;
    QString m_detail;
    double m_value = 0.0;
    qreal m_displayedValue = 0.0;
    QColor m_barColor{59, 130, 246};
    QPropertyAnimation* m_valueAnimation = nullptr;
};
#endif
