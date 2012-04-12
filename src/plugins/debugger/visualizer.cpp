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
    VisualizerNode *node = new VisualizerNode;
    node->setText(*name);

    node->setPos(QPoint(40,
                        50));
    scene->addItem(node);
    return node;
}

void VisualizerWindow::addLink(VisualizerNode* node1, VisualizerNode* node2) {
    VisualizerLink *link = new VisualizerLink(node1, node2);
    scene->addItem(link);
}

VisualizerWindow::VisualizerWindow(QWidget *parent)
  : QGraphicsView(parent)
{
//    setMinimumSize(400, 400);
//    setMaximumSize(400, 400);

    setObjectName(QLatin1String("VisualizerWindow"));
    setWindowTitle(tr("Visualizer"));
    setAcceptDrops(true);
    qDebug() << "VisualizeWindow()";

    scene = new QGraphicsScene(0, 0, 600, 500);
    setScene(scene);
    setDragMode(QGraphicsView::RubberBandDrag);
    setRenderHints(QPainter::Antialiasing
                         | QPainter::TextAntialiasing);
    setContextMenuPolicy(Qt::ActionsContextMenu);

    VisualizerNode *node1 = addNode(new QString("Node 1"));
    VisualizerNode *node2 = addNode(new QString("Node 2"));
    addLink(node1, node2);
}

//void VisualizerWindow::paintEvent(QPaintEvent *event) {
//    QPainter painter;
//    painter.begin(this);
//    painter.drawEllipse(2, 2, 200, 200);
//    painter.end();
//}

void VisualizerWindow::keyPressEvent(QKeyEvent *ev)
{
}

void VisualizerWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasText()) {
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
    }
}

void VisualizerWindow::dragMoveEvent(QDragMoveEvent *ev)
{
    if (ev->mimeData()->hasText()) {
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
    }
}

void VisualizerWindow::dropEvent(QDropEvent *ev)
{
    if (ev->mimeData()->hasText()) {
        watchExpression(ev->mimeData()->text());
        //ev->acceptProposedAction();
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
    }
}

void VisualizerWindow::mouseDoubleClickEvent(QMouseEvent *ev)
{
}

void VisualizerWindow::contextMenuEvent(QContextMenuEvent *ev)
{
    DebuggerEngine *engine = currentEngine();
    WatchHandler *handler = engine->watchHandler();
}

void VisualizerWindow::watchExpression(const QString &exp)
{
}

void VisualizerWindow::removeWatchExpression(const QString &exp)
{
}


} // namespace Internal
} // namespace Debugger

