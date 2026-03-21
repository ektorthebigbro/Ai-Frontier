#ifndef PILL_BADGE_H
#define PILL_BADGE_H
#include <QColor>
#include <QLabel>

class PillBadge : public QLabel {
    Q_OBJECT
public:
    explicit PillBadge(const QString& text = QString(), const QColor& color = QColor(59, 130, 246), QWidget* parent = nullptr);
    void setColor(const QColor& color);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QColor m_color;
};
#endif
