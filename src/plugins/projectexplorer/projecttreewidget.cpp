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

#include "projecttreewidget.h"

#include "projectexplorer.h"
#include "projectnodes.h"
#include "project.h"
#include "session.h"
#include "projectexplorerconstants.h"
#include "projectmodels.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icontext.h>

#include <utils/qtcassert.h>
#include <utils/navigationtreeview.h>

#include <QDebug>
#include <QSettings>

#include <QHeaderView>
#include <QTreeView>
#include <QVBoxLayout>
#include <QToolButton>
#include <QFocusEvent>
#include <QAction>
#include <QPalette>
#include <QMenu>

using namespace ProjectExplorer;
using namespace ProjectExplorer::Internal;

namespace {
    bool debug = false;
}

class ProjectTreeView : public Utils::NavigationTreeView
{
public:
    ProjectTreeView()
    {
        setEditTriggers(QAbstractItemView::EditKeyPressed);
        setContextMenuPolicy(Qt::CustomContextMenu);
//        setExpandsOnDoubleClick(false);
        m_context = new Core::IContext(this);
        m_context->setContext(Core::Context(Constants::C_PROJECT_TREE));
        m_context->setWidget(this);
        Core::ICore::addContextObject(m_context);
    }
    ~ProjectTreeView()
    {
        Core::ICore::removeContextObject(m_context);
        delete m_context;
    }

private:
    Core::IContext *m_context;
};

/*!
  /class ProjectTreeWidget

  Shows the projects in form of a tree.
  */
ProjectTreeWidget::ProjectTreeWidget(QWidget *parent)
        : QWidget(parent),
          m_explorer(ProjectExplorerPlugin::instance()),
          m_view(0),
          m_model(0),
          m_filterProjectsAction(0),
          m_autoSync(false),
          m_autoExpand(true)
{
    m_model = new FlatModel(m_explorer->session()->sessionNode(), this);
    Project *pro = m_explorer->session()->startupProject();
    if (pro)
        m_model->setStartupProject(pro->rootProjectNode());
    NodesWatcher *watcher = new NodesWatcher(this);
    m_explorer->session()->sessionNode()->registerWatcher(watcher);

    connect(watcher, SIGNAL(foldersAboutToBeRemoved(FolderNode *, const QList<FolderNode*> &)),
            this, SLOT(foldersAboutToBeRemoved(FolderNode *, const QList<FolderNode*> &)));
    connect(watcher, SIGNAL(filesAboutToBeRemoved(FolderNode *, const QList<FileNode*> &)),
            this, SLOT(filesAboutToBeRemoved(FolderNode *, const QList<FileNode*> &)));

    m_view = new ProjectTreeView;
    m_view->setModel(m_model);
    setFocusProxy(m_view);
    initView();

    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(m_view);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);

    m_filterProjectsAction = new QAction(tr("Simplify Tree"), this);
    m_filterProjectsAction->setCheckable(true);
    m_filterProjectsAction->setChecked(false); // default is the traditional complex tree
    connect(m_filterProjectsAction, SIGNAL(toggled(bool)), this, SLOT(setProjectFilter(bool)));

    m_filterGeneratedFilesAction = new QAction(tr("Hide Generated Files"), this);
    m_filterGeneratedFilesAction->setCheckable(true);
    m_filterGeneratedFilesAction->setChecked(true);
    connect(m_filterGeneratedFilesAction, SIGNAL(toggled(bool)), this, SLOT(setGeneratedFilesFilter(bool)));

    // connections
    connect(m_model, SIGNAL(modelReset()),
            this, SLOT(initView()));
    connect(m_view, SIGNAL(activated(const QModelIndex&)),
            this, SLOT(openItem(const QModelIndex&)));
    connect(m_view->selectionModel(), SIGNAL(currentChanged(const QModelIndex&, const QModelIndex&)),
            this, SLOT(handleCurrentItemChange(const QModelIndex&)));
    connect(m_view, SIGNAL(customContextMenuRequested(const QPoint&)),
            this, SLOT(showContextMenu(const QPoint&)));
    connect(m_explorer->session(), SIGNAL(singleProjectAdded(ProjectExplorer::Project *)),
            this, SLOT(handleProjectAdded(ProjectExplorer::Project *)));
    connect(m_explorer->session(), SIGNAL(startupProjectChanged(ProjectExplorer::Project *)),
            this, SLOT(startupProjectChanged(ProjectExplorer::Project *)));

    connect(m_explorer->session(), SIGNAL(aboutToLoadSession(QString)),
            this, SLOT(disableAutoExpand()));
    connect(m_explorer->session(), SIGNAL(sessionLoaded(QString)),
            this, SLOT(loadExpandData()));
    connect(m_explorer->session(), SIGNAL(aboutToSaveSession()),
            this, SLOT(saveExpandData()));

    m_toggleSync = new QToolButton;
    m_toggleSync->setIcon(QIcon(QLatin1String(Core::Constants::ICON_LINK)));
    m_toggleSync->setCheckable(true);
    m_toggleSync->setChecked(autoSynchronization());
    m_toggleSync->setToolTip(tr("Synchronize with Editor"));
    connect(m_toggleSync, SIGNAL(clicked(bool)), this, SLOT(toggleAutoSynchronization()));

    setAutoSynchronization(true);
}

void ProjectTreeWidget::disableAutoExpand()
{
    m_autoExpand = false;
}

void ProjectTreeWidget::loadExpandData()
{
    m_autoExpand = true;
    QStringList data = m_explorer->session()->value(QLatin1String("ProjectTree.ExpandData")).toStringList();
    recursiveLoadExpandData(m_view->rootIndex(), data.toSet());
}

void ProjectTreeWidget::recursiveLoadExpandData(const QModelIndex &index, const QSet<QString> &data)
{
    if (data.contains(m_model->nodeForIndex(index)->path())) {
        m_view->expand(index);
        int count = m_model->rowCount(index);
        for (int i = 0; i < count; ++i)
            recursiveLoadExpandData(index.child(i, 0), data);
    }
}

void ProjectTreeWidget::saveExpandData()
{
    QStringList data;
    recursiveSaveExpandData(m_view->rootIndex(), &data);
    // TODO if there are multiple ProjectTreeWidgets, the last one saves the data
    m_explorer->session()->setValue(QLatin1String("ProjectTree.ExpandData"), data);
}

void ProjectTreeWidget::recursiveSaveExpandData(const QModelIndex &index, QStringList *data)
{
    Q_ASSERT(data);
    if (m_view->isExpanded(index)) {
        data->append(m_model->nodeForIndex(index)->path());
        int count = m_model->rowCount(index);
        for (int i = 0; i < count; ++i)
            recursiveSaveExpandData(index.child(i, 0), data);
    }
}

void ProjectTreeWidget::foldersAboutToBeRemoved(FolderNode *, const QList<FolderNode*> &list)
{
    Node *n = m_explorer->currentNode();
    while(n) {
        if (FolderNode *fn = qobject_cast<FolderNode *>(n)) {
            if (list.contains(fn)) {
                ProjectNode *pn = n->projectNode();
                // Make sure the node we are switching too isn't going to be removed also
                while (list.contains(pn))
                    pn = pn->parentFolderNode()->projectNode();
                m_explorer->setCurrentNode(pn);
                break;
            }
        }
        n = n->parentFolderNode();
    }
}

void ProjectTreeWidget::filesAboutToBeRemoved(FolderNode *, const QList<FileNode*> &list)
{
    if (FileNode *fileNode = qobject_cast<FileNode *>(m_explorer->currentNode())) {
        if (list.contains(fileNode)) {
            m_explorer->setCurrentNode(fileNode->projectNode());
        }
    }
}

QToolButton *ProjectTreeWidget::toggleSync()
{
    return m_toggleSync;
}

void ProjectTreeWidget::toggleAutoSynchronization()
{
    setAutoSynchronization(!m_autoSync);
}

bool ProjectTreeWidget::autoSynchronization() const
{
    return m_autoSync;
}

void ProjectTreeWidget::setAutoSynchronization(bool sync, bool syncNow)
{
    m_toggleSync->setChecked(sync);
    if (sync == m_autoSync)
        return;

    m_autoSync = sync;

    if (debug)
        qDebug() << (m_autoSync ? "Enabling auto synchronization" : "Disabling auto synchronization");
    if (m_autoSync) {
        connect(m_explorer, SIGNAL(currentNodeChanged(ProjectExplorer::Node*, ProjectExplorer::Project*)),
                this, SLOT(setCurrentItem(ProjectExplorer::Node*, ProjectExplorer::Project*)));
        if (syncNow)
            setCurrentItem(m_explorer->currentNode(), ProjectExplorerPlugin::currentProject());
    } else {
        disconnect(m_explorer, SIGNAL(currentNodeChanged(ProjectExplorer::Node*, ProjectExplorer::Project*)),
                this, SLOT(setCurrentItem(ProjectExplorer::Node*, ProjectExplorer::Project*)));
    }
}

void ProjectTreeWidget::collapseAll()
{
    m_view->collapseAll();
}

void ProjectTreeWidget::editCurrentItem()
{
    if (m_view->selectionModel()->currentIndex().isValid())
        m_view->edit(m_view->selectionModel()->currentIndex());
}

void ProjectTreeWidget::setCurrentItem(Node *node, Project *project)
{
    if (debug)
        qDebug() << "ProjectTreeWidget::setCurrentItem(" << (project ? project->displayName() : QLatin1String("0"))
                 << ", " <<  (node ? node->path() : QLatin1String("0")) << ")";

    if (!project) {
        return;
    }

    const QModelIndex mainIndex = m_model->indexForNode(node);

    if (mainIndex.isValid() && mainIndex != m_view->selectionModel()->currentIndex()) {
        m_view->setCurrentIndex(mainIndex);
        m_view->scrollTo(mainIndex);
    } else {
        if (debug)
            qDebug() << "clear selection";
        m_view->clearSelection();
    }

}

void ProjectTreeWidget::handleCurrentItemChange(const QModelIndex &current)
{
    Node *node = m_model->nodeForIndex(current);
    // node might be 0. that's okay
    bool autoSync = autoSynchronization();
    setAutoSynchronization(false);
    m_explorer->setCurrentNode(node);
    setAutoSynchronization(autoSync, false);
}

void ProjectTreeWidget::showContextMenu(const QPoint &pos)
{
    QModelIndex index = m_view->indexAt(pos);
    Node *node = m_model->nodeForIndex(index);
    m_explorer->showContextMenu(this, m_view->mapToGlobal(pos), node);
}

void ProjectTreeWidget::handleProjectAdded(ProjectExplorer::Project *project)
{
    Node *node = project->rootProjectNode();
    QModelIndex idx = m_model->indexForNode(node);
    if (m_autoExpand) // disabled while session restoring
        m_view->setExpanded(idx, true);
    m_view->setCurrentIndex(idx);
}

void ProjectTreeWidget::startupProjectChanged(ProjectExplorer::Project *project)
{
    if (project) {
        ProjectNode *node = project->rootProjectNode();
        m_model->setStartupProject(node);
    } else {
        m_model->setStartupProject(0);
    }
}

void ProjectTreeWidget::initView()
{
    QModelIndex sessionIndex = m_model->index(0, 0);

    // hide root folder
    m_view->setRootIndex(sessionIndex);

    while (m_model->canFetchMore(sessionIndex))
        m_model->fetchMore(sessionIndex);

    // expand top level projects
    for (int i = 0; i < m_model->rowCount(sessionIndex); ++i)
        m_view->expand(m_model->index(i, 0, sessionIndex));

    setCurrentItem(m_explorer->currentNode(), ProjectExplorerPlugin::currentProject());
}

void ProjectTreeWidget::openItem(const QModelIndex &mainIndex)
{
    Node *node = m_model->nodeForIndex(mainIndex);
    if (node->nodeType() == FileNodeType) {
        Core::EditorManager *editorManager = Core::EditorManager::instance();
        editorManager->openEditor(node->path(), Core::Id(), Core::EditorManager::ModeSwitch);
    }
}

void ProjectTreeWidget::setProjectFilter(bool filter)
{
    m_model->setProjectFilterEnabled(filter);
    m_filterProjectsAction->setChecked(filter);
}

void ProjectTreeWidget::setGeneratedFilesFilter(bool filter)
{
    m_model->setGeneratedFilesFilterEnabled(filter);
    m_filterGeneratedFilesAction->setChecked(filter);
}

bool ProjectTreeWidget::generatedFilesFilter()
{
    return m_model->generatedFilesFilterEnabled();
}

bool ProjectTreeWidget::projectFilter()
{
    return m_model->projectFilterEnabled();
}


ProjectTreeWidgetFactory::ProjectTreeWidgetFactory()
{
}

ProjectTreeWidgetFactory::~ProjectTreeWidgetFactory()
{
}

QString ProjectTreeWidgetFactory::displayName() const
{
    return tr("Projects");
}

int ProjectTreeWidgetFactory::priority() const
{
    return 100;
}

Core::Id ProjectTreeWidgetFactory::id() const
{
    return Core::Id("Projects");
}

QKeySequence ProjectTreeWidgetFactory::activationSequence() const
{
    return QKeySequence(Qt::ALT + Qt::Key_X);
}

Core::NavigationView ProjectTreeWidgetFactory::createWidget()
{
    Core::NavigationView n;
    ProjectTreeWidget *ptw = new ProjectTreeWidget;
    n.widget = ptw;

    QToolButton *filter = new QToolButton;
    filter->setIcon(QIcon(QLatin1String(Core::Constants::ICON_FILTER)));
    filter->setToolTip(tr("Filter Tree"));
    filter->setPopupMode(QToolButton::InstantPopup);
    filter->setProperty("noArrow", true);
    QMenu *filterMenu = new QMenu(filter);
    filterMenu->addAction(ptw->m_filterProjectsAction);
    filterMenu->addAction(ptw->m_filterGeneratedFilesAction);
    filter->setMenu(filterMenu);

    n.dockToolBarWidgets << filter << ptw->toggleSync();
    return n;
}

void ProjectTreeWidgetFactory::saveSettings(int position, QWidget *widget)
{
    ProjectTreeWidget *ptw = qobject_cast<ProjectTreeWidget *>(widget);
    Q_ASSERT(ptw);
    QSettings *settings = Core::ICore::settings();
    const QString baseKey = QLatin1String("ProjectTreeWidget.") + QString::number(position);
    settings->setValue(baseKey + QLatin1String(".ProjectFilter"), ptw->projectFilter());
    settings->setValue(baseKey + QLatin1String(".GeneratedFilter"), ptw->generatedFilesFilter());
    settings->setValue(baseKey + QLatin1String(".SyncWithEditor"), ptw->autoSynchronization());
}

void ProjectTreeWidgetFactory::restoreSettings(int position, QWidget *widget)
{
    ProjectTreeWidget *ptw = qobject_cast<ProjectTreeWidget *>(widget);
    Q_ASSERT(ptw);
    QSettings *settings = Core::ICore::settings();
    const QString baseKey = QLatin1String("ProjectTreeWidget.") + QString::number(position);
    ptw->setProjectFilter(settings->value(baseKey + QLatin1String(".ProjectFilter"), false).toBool());
    ptw->setGeneratedFilesFilter(settings->value(baseKey + QLatin1String(".GeneratedFilter"), true).toBool());
    ptw->setAutoSynchronization(settings->value(baseKey +  QLatin1String(".SyncWithEditor"), true).toBool());
}
