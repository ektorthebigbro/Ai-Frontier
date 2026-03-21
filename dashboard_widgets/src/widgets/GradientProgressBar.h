#ifndef GRADIENT_PROGRESS_BAR_H
#define GRADIENT_PROGRESS_BAR_H
#include <QSize>
#include <QWidget>

class QPropertyAnimation;
class QVariantAnimation;

class GradientProgressBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal displayedValue READ displayedValue WRITE setDisplayedValue)
public:
    explicit GradientProgressBar(QWidget* parent = nullptr);
    void setValue(double value); // 0.0 to 1.0
    void setText(const QString& text);
    qreal displayedValue() const { return m_displayedValue; }
    void setDisplayedValue(qreal value);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    double m_value = 0.0;
    qreal m_displayedValue = 0.0;
    qreal m_shimmerPhase = 0.0;
    QString m_text;
    QPropertyAnimation* m_valueAnimation = nullptr;
    QVariantAnimation* m_shimmerAnimation = nullptr;
};
#endif
