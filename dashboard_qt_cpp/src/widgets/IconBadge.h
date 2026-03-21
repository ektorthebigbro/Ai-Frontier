#ifndef ICON_BADGE_H
#define ICON_BADGE_H
#include <QColor>
#include <QWidget>

class IconBadge : public QWidget {
    Q_OBJECT
public:
    explicit IconBadge(const QString& icon, const QColor& color, int size = 48, QWidget* parent = nullptr);
    void setIcon(const QString& icon);
    void setColor(const QColor& color);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QString m_icon;
    QColor m_color;
    int m_size;
};
#endif
