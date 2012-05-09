/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
** If you have questions regarding the use of this file, please contact
** Nokia at qt-info@nokia.com.
**
**************************************************************************/

#include "visualizer.h"
#include "visualizer_link.h"
#include "visualizer_node.h"

#include "breakhandler.h"
#include "registerhandler.h"
#include "debuggeractions.h"
#include "debuggerconstants.h"
#include "debuggerinternalconstants.h"
#include "debuggercore.h"
#include "debuggerdialogs.h"
#include "debuggerengine.h"
#include "debuggerstartparameters.h"
#include "watchdelegatewidgets.h"
#include "watchhandler.h"
#include "debuggertooltipmanager.h"
#include "memoryagent.h"

#include <utils/qtcassert.h>
#include <utils/savedaction.h>

#include <QDebug>
#include <QMetaObject>
#include <QMetaProperty>
#include <QVariant>
#include <QMimeData>

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QScrollBar>
#include <QApplication>
#include <QPalette>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QItemDelegate>
#include <QMenu>
#include <QPainter>
#include <QInputDialog>
#include <QMessageBox>

namespace Debugger {
namespace Internal {

static DebuggerEngine *currentEngine()
{
    return debuggerCore()->currentEngine();
}

VisualizerNode* VisualizerWindow::addNode(QString *name) {
    VisualizerNode *node = new VisualizerNode(viewport());
    node->setText(*name);
    node->setPos(QPoint(40, 50));

    scene.addItem(node);
    return node;
}

void VisualizerWindow::addLink(VisualizerNode* node1, VisualizerNode* node2) {
    VisualizerLink *link = new VisualizerLink(node1, node2);
    scene.addItem(link);
}

void VisualizerWindow::setupNodes()
{
    VisualizerNode *node1 = addNode(new QString("Node 1"));
    node1->setPos(20, 30);
    VisualizerNode *node2 = addNode(new QString("Node 2"));
    node2->setPos(60, 70);
    addLink(node1, node2);
}

VisualizerWindow::VisualizerWindow(QWidget *parent)
    : QAbstractItemView(parent)
{
    setObjectName(QLatin1String("VisualizerWindow"));
    setWindowTitle(tr("Visualizer"));
    setAcceptDrops(true);

    scene.setSceneRect(0, 0, 600, 500);
    gview.setScene(&scene);
    setViewport(&gview);

    //setupNodes();
}

QString VisualizerWindow::getWatchData(QModelIndex index) {
    QString name(model()->data(index, LocalsNameRole).toString());
    QString value(model()->data(index, LocalsRawValueRole).toString());
    QString type(model()->data(index, LocalsTypeRole).toString());
    QString rawType(model()->data(index, LocalsRawTypeRole).toString());
    QString expandRole(model()->data(index, LocalsExpandedRole).toString());

    //qDebug() << name << ":" << value << "@" << type;
    return name + ":" + type + "@" + value;
}

void VisualizerWindow::rowsInserted(const QModelIndex &parent, int start, int end)
{
    for (int row = start; row <= end; ++row) {
        QModelIndex index = model()->index(row, 1, rootIndex());
        QString name(model()->data(index, LocalsNameRole).toString());

        if(nodemap.contains(name)) {
            VisualizerNode *node = nodemap.find(name).value();
            node->setText(getWatchData(index));
        } else {
            VisualizerNode *node = addNode(&getWatchData(index));
            nodemap.insert(name, node);
        }
    }

    QAbstractItemView::rowsInserted(parent, start, end);
}


void VisualizerWindow::dataChanged(const QModelIndex &topLeft,
                                   const QModelIndex &bottomRight)
{
    QAbstractItemView::dataChanged(topLeft, bottomRight);

    for (int row = 0; row < model()->rowCount(rootIndex()); ++row) {
        QModelIndex index = model()->index(row, 1, rootIndex());
        QString name(model()->data(index, LocalsNameRole).toString());

        if(nodemap.contains(name)) {
            VisualizerNode *node = nodemap.find(name).value();
            node->setText(getWatchData(index));
        }

        QString type(model()->data(index, LocalsTypeRole).toString());
        if(type == "QVector<QString>") {
            qDebug() << name << " has children " << model()->hasChildren(index)
                     << " dimen " << model()->rowCount(index) << "x" <<
                        model()->columnCount(index);

            QModelIndex childIndex1 = model()->index(0, 0, index);
            QModelIndex childIndex2 = model()->index(0, 2, index);
            qDebug() << "Child 0,0: " << getWatchData(childIndex1);
            qDebug() << "Child 0,2: " << getWatchData(childIndex2);
        }
    }
    viewport()->update();
}

void VisualizerWindow::rowsAboutToBeRemoved(const QModelIndex &parent,
                                            int start, int end)
{
    for (int row = start; row <= end; ++row) {
        QModelIndex index = model()->index(row, 1, rootIndex());
        QString name(model()->data(index, LocalsNameRole).toString());

        if(nodemap.contains(name)) {
            VisualizerNode *node = nodemap.find(name).value();
            scene.removeItem(node);
            nodemap.remove(name);
        }
    }

    QAbstractItemView::rowsAboutToBeRemoved(parent, start, end);
}

/***** ****/

void VisualizerWindow::paintEvent(QPaintEvent *event) {
}

void VisualizerWindow::mouseReleaseEvent(QMouseEvent *event)
{
    QAbstractItemView::mouseReleaseEvent(event);
    viewport()->update();
}

QModelIndex VisualizerWindow::moveCursor(QAbstractItemView::CursorAction cursorAction,
                                         Qt::KeyboardModifiers modifiers)
{
    QModelIndex current = currentIndex();
    viewport()->update();
    return current;
}

int VisualizerWindow::horizontalOffset() const
{
    return horizontalScrollBar()->value();
}

int VisualizerWindow::verticalOffset() const
{
    return verticalScrollBar()->value();
}

void VisualizerWindow::resizeEvent(QResizeEvent * /* event */)
{
    updateGeometries();
}

void VisualizerWindow::scrollContentsBy(int dx, int dy)
{
    viewport()->scroll(dx, dy);
}

bool VisualizerWindow::isIndexHidden(const QModelIndex & /*index*/) const
{
    return false;
}

QModelIndex VisualizerWindow::indexAt(const QPoint &point) const
{
    return QModelIndex();
}

QRect VisualizerWindow::visualRect(const QModelIndex &index) const
{}

void VisualizerWindow::scrollTo(const QModelIndex &index, ScrollHint)
{}


void VisualizerWindow::setSelection(const QRect &rect, QItemSelectionModel::SelectionFlags command)
{}

QRegion VisualizerWindow::visualRegionForSelection(const QItemSelection &selection) const
{}


} // namespace Internal
} // namespace Debugger

