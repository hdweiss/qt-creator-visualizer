#include <QtGui>

#include "visualizer_link.h"
#include "visualizer_node.h"

VisualizerLink::VisualizerLink(VisualizerNode *fromVisualizerNode, VisualizerNode *toVisualizerNode)
{
    myFromVisualizerNode = fromVisualizerNode;
    myToVisualizerNode = toVisualizerNode;

    myFromVisualizerNode->addVisualizerLink(this);
    myToVisualizerNode->addVisualizerLink(this);

    setFlags(QGraphicsItem::ItemIsSelectable);
    setZValue(-1);

    setColor(Qt::darkRed);
    trackVisualizerNodes();
}

VisualizerLink::~VisualizerLink()
{
    myFromVisualizerNode->removeVisualizerLink(this);
    myToVisualizerNode->removeVisualizerLink(this);
}

VisualizerNode *VisualizerLink::fromVisualizerNode() const
{
    return myFromVisualizerNode;
}

VisualizerNode *VisualizerLink::toVisualizerNode() const
{
    return myToVisualizerNode;
}

void VisualizerLink::setColor(const QColor &color)
{
    setPen(QPen(color, 1.0));
}

QColor VisualizerLink::color() const
{
    return pen().color();
}

void VisualizerLink::trackVisualizerNodes()
{
    setLine(QLineF(myFromVisualizerNode->pos(), myToVisualizerNode->pos()));
}
