#ifndef VisualizerLink_H
#define VisualizerLink_H

#include <QGraphicsLineItem>

class VisualizerNode;

class VisualizerLink : public QGraphicsLineItem
{
public:
    VisualizerLink(VisualizerNode *fromVisualizerNode,
                   VisualizerNode *toVisualizerNode);
    ~VisualizerLink();

    VisualizerNode *fromVisualizerNode() const;
    VisualizerNode *toVisualizerNode() const;

    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option, QWidget *widget);

    void setColor(const QColor &color);
    QColor color() const;

    void trackVisualizerNodes();

private:
    VisualizerNode *myFromVisualizerNode;
    VisualizerNode *myToVisualizerNode;
};

#endif
