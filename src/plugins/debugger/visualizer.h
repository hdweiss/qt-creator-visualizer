#ifndef DEBUGGER_VISUALIZER_H
#define DEBUGGER_VISUALIZER_H

#include "basewindow.h"
#include "visualizer_link.h"
#include "visualizer_node.h"
#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsView>

namespace Debugger {
namespace Internal {

/////////////////////////////////////////////////////////////////////
//
// Visualizer
//
/////////////////////////////////////////////////////////////////////

class VisualizerWindow : public QAbstractItemView
{
    Q_OBJECT

public:
    explicit VisualizerWindow(QWidget *parent = 0);
    QRect visualRect(const QModelIndex &index) const;
    void scrollTo(const QModelIndex &index, ScrollHint hint = EnsureVisible);
    QModelIndex indexAt(const QPoint &point) const;

protected slots:
    void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    void rowsInserted(const QModelIndex &parent, int start, int end);
    void rowsAboutToBeRemoved(const QModelIndex &parent, int start, int end);

protected:

    QModelIndex moveCursor(QAbstractItemView::CursorAction cursorAction,
                           Qt::KeyboardModifiers modifiers);

    int horizontalOffset() const;
    int verticalOffset() const;

    bool isIndexHidden(const QModelIndex &index) const;

    void setSelection(const QRect&, QItemSelectionModel::SelectionFlags command);

    void mouseReleaseEvent(QMouseEvent *event);
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *event);
    void scrollContentsBy(int dx, int dy);

    QRegion visualRegionForSelection(const QItemSelection &selection) const;

private:
    VisualizerNode* addNode(QString *name);
    void addLink(VisualizerNode *node1, VisualizerNode *node2);
    void setupNodes();
    QString getWatchData(QModelIndex index);

    QGraphicsScene scene;
    QGraphicsView gview;

    // Hack
    QMultiHash<QString, VisualizerNode*> nodemap;

};


} // namespace Internal
} // namespace Debugger

#endif // DEBUGGER_VISUALIZERWINDOW_H
