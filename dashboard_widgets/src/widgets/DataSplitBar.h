#ifndef DATA_SPLIT_BAR_H
#define DATA_SPLIT_BAR_H
#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

struct SplitSegment {
    QString label;
    double fraction; // 0.0-1.0
    QColor color;
};

class DataSplitBar : public QWidget {
    Q_OBJECT
public:
    explicit DataSplitBar(QWidget* parent = nullptr);
    void setSegments(const QVector<SplitSegment>& segments);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QVector<SplitSegment> m_segments;
};
#endif
