#ifndef VisualizerNode_H
#define VisualizerNode_H

#include <QApplication>
#include <QColor>
#include <QGraphicsItem>
#include <QSet>

class VisualizerLink;

class VisualizerNode : public QGraphicsItem
{
    Q_DECLARE_TR_FUNCTIONS(VisualizerNode)

public:
    VisualizerNode();
    ~VisualizerNode();

    void setText(const QString &text);
    QString text() const;
    void setTextColor(const QColor &color);
    QColor textColor() const;
    void setOutlineColor(const QColor &color);
    QColor outlineColor() const;
    void setBackgroundColor(const QColor &color);
    QColor backgroundColor() const;

    void addVisualizerLink(VisualizerLink *VisualizerLink);
    void removeVisualizerLink(VisualizerLink *VisualizerLink);

    QRectF boundingRect() const;
    QPainterPath shape() const;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option, QWidget *widget);

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event);
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant &value);

private:
    QRectF outlineRect() const;
    int roundness(double size) const;

    QSet<VisualizerLink *> myVisualizerLinks;
    QString myText;
    QColor myTextColor;
    QColor myBackgroundColor;
    QColor myOutlineColor;
};

#endif
