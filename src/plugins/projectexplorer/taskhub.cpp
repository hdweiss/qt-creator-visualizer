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

#include "taskhub.h"
#include "extensionsystem/pluginmanager.h"
#include "projectexplorer.h"
#include <texteditor/basetextmark.h>
#include <QMetaType>

using namespace ProjectExplorer;

class TaskMark : public TextEditor::BaseTextMark
{
public:
    TaskMark(unsigned int id, const QString &fileName, int lineNumber, bool visible)
        : BaseTextMark(fileName, lineNumber), m_id(id), m_visible(visible)
    {}

    void updateLineNumber(int lineNumber);
    void removedFromEditor();
    bool visible() const;
private:
    unsigned int m_id;
    bool m_visible;
};

void TaskMark::updateLineNumber(int lineNumber)
{
    ProjectExplorerPlugin::instance()->taskHub()->updateTaskLineNumber(m_id, lineNumber);
    BaseTextMark::updateLineNumber(lineNumber);
}

void TaskMark::removedFromEditor()
{
    ProjectExplorerPlugin::instance()->taskHub()->updateTaskLineNumber(m_id, -1);
}

bool TaskMark::visible() const
{
    return m_visible;
}

TaskHub::TaskHub()
    : m_errorIcon(QLatin1String(":/projectexplorer/images/compile_error.png")),
      m_warningIcon(QLatin1String(":/projectexplorer/images/compile_warning.png"))
{
    qRegisterMetaType<ProjectExplorer::Task>("ProjectExplorer::Task");
    qRegisterMetaType<QList<ProjectExplorer::Task> >("QList<ProjectExplorer::Task>");
}

TaskHub::~TaskHub()
{

}

void TaskHub::addCategory(const Core::Id &categoryId, const QString &displayName, bool visible)
{
    emit categoryAdded(categoryId, displayName, visible);
}

void TaskHub::addTask(Task task)
{
    if (task.line != -1 && !task.file.isEmpty()) {
        bool visible = (task.type == Task::Warning || task.type == Task::Error);
        TaskMark *mark = new TaskMark(task.taskId, task.file.toString(), task.line, visible);
        mark->setIcon(taskTypeIcon(task.type));
        mark->setPriority(TextEditor::ITextMark::HighPriority);
        task.addMark(mark);
    }
    emit taskAdded(task);
}

void TaskHub::clearTasks(const Core::Id &categoryId)
{
    emit tasksCleared(categoryId);
}

void TaskHub::removeTask(const Task &task)
{
    emit taskRemoved(task);
}

void TaskHub::updateTaskLineNumber(unsigned int id, int line)
{
    emit taskLineNumberUpdated(id, line);
}

void TaskHub::setCategoryVisibility(const Core::Id &categoryId, bool visible)
{
    emit categoryVisibilityChanged(categoryId, visible);
}

void TaskHub::popup(bool withFocus)
{
    emit popupRequested(withFocus);
}

QIcon TaskHub::taskTypeIcon(Task::TaskType t) const
{
    switch (t) {
    case Task::Warning:
        return m_warningIcon;
    case Task::Error:
        return m_errorIcon;
    case Task::Unknown:
        break;
    }
    return QIcon();
}

