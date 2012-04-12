#include <QtGui>

#include "visualizer_link.h"
#include "visualizer_node.h"

VisualizerNode::VisualizerNode()
{
    myTextColor = Qt::darkGreen;
    myOutlineColor = Qt::darkBlue;
    myBackgroundColor = Qt::white;

    setFlags(ItemIsMovable | ItemIsSelectable);
}

VisualizerNode::~VisualizerNode()
{
    foreach (VisualizerLink *VisualizerLink, myVisualizerLinks)
        delete VisualizerLink;
}

void VisualizerNode::setText(const QString &text)
{
    prepareGeometryChange();
    myText = text;
    update();
}

QString VisualizerNode::text() const
{
    return myText;
}

void VisualizerNode::setTextColor(const QColor &color)
{
    myTextColor = color;
    update();
}

QColor VisualizerNode::textColor() const
{
    return myTextColor;
}

void VisualizerNode::setOutlineColor(const QColor &color)
{
    myOutlineColor = color;
    update();
}

QColor VisualizerNode::outlineColor() const
{
    return myOutlineColor;
}

void VisualizerNode::setBackgroundColor(const QColor &color)
{
    myBackgroundColor = color;
    update();
}

QColor VisualizerNode::backgroundColor() const
{
    return myBackgroundColor;
}

void VisualizerNode::addVisualizerLink(VisualizerLink *VisualizerLink)
{
    myVisualizerLinks.insert(VisualizerLink);
}

void VisualizerNode::removeVisualizerLink(VisualizerLink *VisualizerLink)
{
    myVisualizerLinks.remove(VisualizerLink);
}

QRectF VisualizerNode::boundingRect() const
{
    const int Margin = 1;
    return outlineRect().adjusted(-Margin, -Margin, +Margin, +Margin);
}

QPainterPath VisualizerNode::shape() const
{
    QRectF rect = outlineRect();

    QPainterPath path;
    path.addRoundRect(rect, roundness(rect.width()),
                      roundness(rect.height()));
    return path;
}

void VisualizerNode::paint(QPainter *painter,
                 const QStyleOptionGraphicsItem *option,
                 QWidget * /* widget */)
{
    QPen pen(myOutlineColor);
    if (option->state & QStyle::State_Selected) {
        pen.setStyle(Qt::DotLine);
        pen.setWidth(2);
    }
    painter->setPen(pen);
    painter->setBrush(myBackgroundColor);

    QRectF rect = outlineRect();
    painter->drawRoundRect(rect, roundness(rect.width()),
                           roundness(rect.height()));

    painter->setPen(myTextColor);
    painter->drawText(rect, Qt::AlignCenter, myText);
}

void VisualizerNode::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    QString text = QInputDialog::getText(event->widget(),
                           tr("Edit Text"), tr("Enter new text:"),
                           QLineEdit::Normal, myText);
    if (!text.isEmpty())
        setText(text);
}

QVariant VisualizerNode::itemChange(GraphicsItemChange change,
                          const QVariant &value)
{
    if (change == ItemPositionHasChanged) {
        foreach (VisualizerLink *VisualizerLink, myVisualizerLinks)
            VisualizerLink->trackVisualizerNodes();
    }
    return QGraphicsItem::itemChange(change, value);
}

QRectF VisualizerNode::outlineRect() const
{
    const int Padding = 8;
    QFontMetricsF metrics = qApp->font();
    QRectF rect = metrics.boundingRect(myText);
    rect.adjust(-Padding, -Padding, +Padding, +Padding);
    rect.translate(-rect.center());
    return rect;
}

int VisualizerNode::roundness(double size) const
{
    const int Diameter = 12;
    return 100 * Diameter / int(size);
}
