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

#include "session.h"

#include "project.h"
#include "projectexplorer.h"
#include "projectexplorerconstants.h"
#include "nodesvisitor.h"
#include "editorconfiguration.h"
#include "projectnodes.h"

#include <coreplugin/icore.h>
#include <coreplugin/imode.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/progressmanager/progressmanager.h>
#include <coreplugin/modemanager.h>

#include <texteditor/itexteditor.h>

#include <utils/listutils.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QSettings>
#include <QTimer>

#include <QApplication>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QTextCodec>

namespace {
    bool debug = false;
}

using namespace Core;
using Utils::PersistentSettingsReader;
using Utils::PersistentSettingsWriter;

using namespace ProjectExplorer;
using namespace ProjectExplorer::Internal;

/*!
     \class ProjectExplorer::SessionManager

     \brief Session management.

     TODO the interface of this class is not really great.
     The implementation suffers that all the functions from the
     public interface just wrap around functions which do the actual work
     This could be improved.
*/

SessionManager::SessionManager(QObject *parent)
  : QObject(parent),
    m_sessionNode(new SessionNode(this)),
    m_sessionName(QLatin1String("default")),
    m_virginSession(true),
    m_startupProject(0)
{
    connect(ModeManager::instance(), SIGNAL(currentModeChanged(Core::IMode*)),
            this, SLOT(saveActiveMode(Core::IMode*)));

    EditorManager *em = ICore::editorManager();

    connect(em, SIGNAL(editorCreated(Core::IEditor *, QString)),
            this, SLOT(configureEditor(Core::IEditor *, QString)));
    connect(ProjectExplorerPlugin::instance(), SIGNAL(currentProjectChanged(ProjectExplorer::Project *)),
            this, SLOT(updateWindowTitle()));
    connect(em, SIGNAL(editorOpened(Core::IEditor*)),
            this, SLOT(markSessionFileDirty()));
    connect(em, SIGNAL(editorsClosed(QList<Core::IEditor*>)),
            this, SLOT(markSessionFileDirty()));

    m_autoSaveSessionTimer = new QTimer(this);
    m_autoSaveSessionTimer->setSingleShot(true);
    m_autoSaveSessionTimer->setInterval(10000);
    connect(m_autoSaveSessionTimer, SIGNAL(timeout()),
            ICore::instance(), SIGNAL(saveSettingsRequested()));
}

SessionManager::~SessionManager()
{
    emit aboutToUnloadSession(m_sessionName);
}


bool SessionManager::isDefaultVirgin() const
{
    return isDefaultSession(m_sessionName)
            && m_virginSession;
}

bool SessionManager::isDefaultSession(const QString &session) const
{
    return session == QLatin1String("default");
}


void SessionManager::saveActiveMode(Core::IMode *mode)
{
    setValue(QLatin1String("ActiveMode"), mode->id());
}

void SessionManager::clearProjectFileCache()
{
    // If triggered by the fileListChanged signal of one project
    // only invalidate cache for this project
    Project *pro = qobject_cast<Project*>(sender());
    if (pro)
        m_projectFileCache.remove(pro);
    else
        m_projectFileCache.clear();
}

bool SessionManager::recursiveDependencyCheck(const QString &newDep, const QString &checkDep) const
{
    if (newDep == checkDep)
        return false;

    foreach (const QString &dependency, m_depMap.value(checkDep)) {
        if (!recursiveDependencyCheck(newDep, dependency))
            return false;
    }

    return true;
}

/*
 * TODO: The dependency management exposes an interface based on projects, but
 * is internally purely string based. This is suboptimal. Probably it would be
 * nicer to map the filenames to projects on load and only map it back to
 * filenames when saving.
 */

QList<Project *> SessionManager::dependencies(const Project *project) const
{
    const QString &proName = project->document()->fileName();
    const QStringList &proDeps = m_depMap.value(proName);

    QList<Project *> projects;
    foreach (const QString &dep, proDeps) {
        if (Project *pro = projectForFile(dep))
            projects += pro;
    }

    return projects;
}

bool SessionManager::hasDependency(const Project *project, const Project *depProject) const
{
    const QString &proName = project->document()->fileName();
    const QString &depName = depProject->document()->fileName();

    const QStringList &proDeps = m_depMap.value(proName);
    return proDeps.contains(depName);
}

bool SessionManager::canAddDependency(const Project *project, const Project *depProject) const
{
    const QString &newDep = project->document()->fileName();
    const QString &checkDep = depProject->document()->fileName();

    return recursiveDependencyCheck(newDep, checkDep);
}

bool SessionManager::addDependency(Project *project, Project *depProject)
{
    const QString &proName = project->document()->fileName();
    const QString &depName = depProject->document()->fileName();

    // check if this dependency is valid
    if (!recursiveDependencyCheck(proName, depName))
        return false;

    QStringList proDeps = m_depMap.value(proName);
    if (!proDeps.contains(depName)) {
        proDeps.append(depName);
        m_depMap[proName] = proDeps;
    }
    emit dependencyChanged(project, depProject);

    return true;
}

void SessionManager::removeDependency(Project *project, Project *depProject)
{
    const QString &proName = project->document()->fileName();
    const QString &depName = depProject->document()->fileName();

    QStringList proDeps = m_depMap.value(proName);
    proDeps.removeAll(depName);
    if (proDeps.isEmpty()) {
        m_depMap.remove(proName);
    } else {
        m_depMap[proName] = proDeps;
    }
    emit dependencyChanged(project, depProject);
}

void SessionManager::setStartupProject(Project *startupProject)
{
    if (debug)
        qDebug() << Q_FUNC_INFO << (startupProject ? startupProject->displayName() : QLatin1String("0"));

    if (startupProject) {
        Q_ASSERT(m_projects.contains(startupProject));
    }

    if (m_startupProject == startupProject)
        return;

    m_startupProject = startupProject;
    emit startupProjectChanged(startupProject);
}

Project *SessionManager::startupProject() const
{
    return m_startupProject;
}

void SessionManager::addProject(Project *project)
{
    addProjects(QList<Project*>() << project);
}

void SessionManager::addProjects(const QList<Project*> &projects)
{
    m_virginSession = false;
    QList<Project*> clearedList;
    foreach (Project *pro, projects) {
        if (!m_projects.contains(pro)) {
            clearedList.append(pro);
            m_projects.append(pro);
            m_sessionNode->addProjectNodes(QList<ProjectNode *>() << pro->rootProjectNode());

            connect(pro, SIGNAL(fileListChanged()),
                    this, SLOT(clearProjectFileCache()));

            if (debug)
                qDebug() << "SessionManager - adding project " << pro->displayName();
        }
    }

    foreach (Project *pro, clearedList) {
        emit projectAdded(pro);
    }

    if (clearedList.count() == 1)
        emit singleProjectAdded(clearedList.first());

    // maybe we have a new startup project?
    if (!startupProject())
        if (!m_projects.isEmpty())
            setStartupProject(m_projects.first());
}

void SessionManager::removeProject(Project *project)
{
    m_virginSession = false;
    if (project == 0) {
        qDebug() << "SessionManager::removeProject(0) ... THIS SHOULD NOT HAPPEN";
        return;
    }
    removeProjects(QList<Project*>() << project);
}

bool SessionManager::save()
{
    if (debug)
        qDebug() << "SessionManager - saving session" << m_sessionName;

    emit aboutToSaveSession();

    PersistentSettingsWriter writer;

    // save the startup project
    if (m_startupProject) {
        writer.saveValue(QLatin1String("StartupProject"), m_startupProject->document()->fileName());
    }

    QStringList projectFiles;
    foreach (Project *pro, m_projects)
        projectFiles << pro->document()->fileName();

    // Restore infromation on projects that failed to load:
    projectFiles.append(m_failedProjects);

    writer.saveValue(QLatin1String("ProjectList"), projectFiles);

    QMap<QString, QVariant> depMap;
    QMap<QString, QStringList>::const_iterator i = m_depMap.constBegin();
    while (i != m_depMap.constEnd()) {
        QString key = i.key();
        QStringList values;
        foreach (const QString &value, i.value()) {
            values << value;
        }
        depMap.insert(key, values);
        ++i;
    }
    writer.saveValue(QLatin1String("ProjectDependencies"), QVariant(depMap));

    int editorCount = 0;
    QList<Core::IEditor *> editors = ICore::editorManager()->openedEditors();
    foreach (Core::IEditor *editor, editors) {
        Q_ASSERT(editor);
        if (!editor->isTemporary())
            ++editorCount;
    }
    writer.saveValue(QLatin1String("OpenEditors"), editorCount);
    writer.saveValue(QLatin1String("EditorSettings"),
                     ICore::editorManager()->saveState().toBase64());

    QMap<QString, QVariant>::const_iterator it, end;
    end = m_values.constEnd();
    QStringList keys;
    for (it = m_values.constBegin(); it != end; ++it) {
        writer.saveValue(QLatin1String("value-") + it.key(), it.value());
        keys << it.key();
    }

    writer.saveValue(QLatin1String("valueKeys"), keys);

    QString fileName = sessionNameToFileName(m_sessionName);
    bool result = writer.save(fileName, QLatin1String("QtCreatorSession"), Core::ICore::mainWindow());
    if (!result) {
        QMessageBox::warning(0, tr("Error while saving session"),
                                tr("Could not save session to file %1").arg(fileName));
    }

    if (debug)
        qDebug() << "SessionManager - saving session returned " << result;

    return result;
}

/*!
  \fn bool SessionManager::closeAllProjects()

  Closes all projects
  */
void SessionManager::closeAllProjects()
{
    setStartupProject(0);
    removeProjects(projects());
}

const QList<Project *> &SessionManager::projects() const
{
    return m_projects;
}

QStringList SessionManager::dependencies(const QString &proName) const
{
    QStringList result;
    foreach (const QString &dep, m_depMap.value(proName))
        result += dependencies(dep);

    result << proName;
    return result;
}

QStringList SessionManager::dependenciesOrder() const
{
    QList<QPair<QString, QStringList> > unordered;
    QStringList ordered;

    // copy the map to a temporary list
    foreach (Project *pro, projects()) {
        const QString &proName = pro->document()->fileName();
        unordered << QPair<QString, QStringList>
            (proName, m_depMap.value(proName));
    }

    while (!unordered.isEmpty()) {
        for (int i=(unordered.count()-1); i>=0; --i) {
            if (unordered.at(i).second.isEmpty()) {
                ordered << unordered.at(i).first;
                unordered.removeAt(i);
            }
        }

        // remove the handled projects from the dependency lists
        // of the remaining unordered projects
        for (int i = 0; i < unordered.count(); ++i) {
            foreach (const QString &pro, ordered) {
                QStringList depList = unordered.at(i).second;
                depList.removeAll(pro);
                unordered[i].second = depList;
            }
        }
    }

    return ordered;
}

QList<Project *> SessionManager::projectOrder(Project *project) const
{
    QList<Project *> result;

    QStringList pros;
    if (project) {
        pros = dependencies(project->document()->fileName());
    } else {
        pros = dependenciesOrder();
    }

    foreach (const QString &proFile, pros) {
        foreach (Project *pro, projects()) {
            if (pro->document()->fileName() == proFile) {
                result << pro;
                break;
            }
        }
    }

    return result;
}

Project *SessionManager::projectForNode(Node *node) const
{
    if (!node)
        return 0;

    Project *project = 0;

    FolderNode *rootProjectNode = qobject_cast<FolderNode*>(node);
    if (!rootProjectNode)
        rootProjectNode = node->parentFolderNode();
    while (rootProjectNode && rootProjectNode->parentFolderNode() != m_sessionNode)
        rootProjectNode = rootProjectNode->parentFolderNode();

    Q_ASSERT(rootProjectNode);

    QList<Project *> projectList = projects();
    foreach (Project *p, projectList) {
        if (p->rootProjectNode() == rootProjectNode) {
            project = p;
            break;
        }
    }

    return project;
}

Node *SessionManager::nodeForFile(const QString &fileName, Project *project) const
{
    Node *node = 0;
    if (!project)
        project = projectForFile(fileName);
    if (project) {
        FindNodesForFileVisitor findNodes(fileName);
        project->rootProjectNode()->accept(&findNodes);

        foreach (Node *n, findNodes.nodes()) {
            // prefer file nodes
            if (!node || (node->nodeType() != FileNodeType && n->nodeType() == FileNodeType))
                node = n;
        }
    }

    return node;
}

Project *SessionManager::projectForFile(const QString &fileName) const
{
    if (debug)
        qDebug() << "SessionManager::projectForFile(" << fileName << ")";

    const QList<Project *> &projectList = projects();

    // Check current project first
    Project *currentProject = ProjectExplorerPlugin::currentProject();
    if (currentProject && projectContainsFile(currentProject, fileName))
        return currentProject;

    foreach (Project *p, projectList)
        if (p != currentProject && projectContainsFile(p, fileName))
            return p;
    return 0;
}

bool SessionManager::projectContainsFile(Project *p, const QString &fileName) const
{
    if (!m_projectFileCache.contains(p))
        m_projectFileCache.insert(p, p->files(Project::AllFiles));

    return m_projectFileCache.value(p).contains(fileName);
}

void SessionManager::configureEditor(Core::IEditor *editor, const QString &fileName)
{
    if (TextEditor::ITextEditor *textEditor = qobject_cast<TextEditor::ITextEditor*>(editor)) {
        Project *project = projectForFile(fileName);
        // Global settings are the default.
        if (project)
            project->editorConfiguration()->configureEditor(textEditor);
    }
}

void SessionManager::updateWindowTitle()
{
    if (isDefaultSession(m_sessionName)) {
        if (Project *currentProject = ProjectExplorerPlugin::currentProject())
            ICore::editorManager()->setWindowTitleAddition(currentProject->displayName());
        else
            ICore::editorManager()->setWindowTitleAddition(QString());
    } else {
        QString sessionName = m_sessionName;
        if (sessionName.isEmpty())
            sessionName = tr("Untitled");
        ICore::editorManager()->setWindowTitleAddition(sessionName);
    }
}

void SessionManager::removeProjects(QList<Project *> remove)
{
    QMap<QString, QStringList> resMap;

    foreach (Project *pro, remove) {
        if (debug)
            qDebug() << "SessionManager - emitting aboutToRemoveProject(" << pro->displayName() << ")";
        emit aboutToRemoveProject(pro);
    }


    // Refresh dependencies
    QSet<QString> projectFiles;
    foreach (Project *pro, projects()) {
        if (!remove.contains(pro))
            projectFiles.insert(pro->document()->fileName());
    }

    QSet<QString>::const_iterator i = projectFiles.begin();
    while (i != projectFiles.end()) {
        QStringList dependencies;
        foreach (const QString &dependency, m_depMap.value(*i)) {
            if (projectFiles.contains(dependency))
                dependencies << dependency;
        }
        if (!dependencies.isEmpty())
            resMap.insert(*i, dependencies);
        ++i;
    }

    m_depMap = resMap;

    // TODO: Clear m_modelProjectHash

    // Delete projects
    foreach (Project *pro, remove) {
        pro->saveSettings();
        m_projects.removeOne(pro);

        if (pro == m_startupProject)
            setStartupProject(0);

        disconnect(pro, SIGNAL(fileListChanged()),
                this, SLOT(clearProjectFileCache()));
        m_projectFileCache.remove(pro);

        if (debug)
            qDebug() << "SessionManager - emitting projectRemoved(" << pro->displayName() << ")";
        m_sessionNode->removeProjectNodes(QList<ProjectNode *>() << pro->rootProjectNode());
        emit projectRemoved(pro);
        delete pro;
    }

    if (startupProject() == 0)
        if (!m_projects.isEmpty())
            setStartupProject(m_projects.first());
}

/*!
    \brief Let other plugins store persistent values within the session file.
*/

void SessionManager::setValue(const QString &name, const QVariant &value)
{
    if (m_values.value(name) == value)
        return;
    m_values.insert(name, value);
    markSessionFileDirty(false);
}

QVariant SessionManager::value(const QString &name)
{
    QMap<QString, QVariant>::const_iterator it = m_values.find(name);
    return (it == m_values.constEnd()) ? QVariant() : *it;
}

QString SessionManager::activeSession() const
{
    return m_sessionName;
}

QStringList SessionManager::sessions() const
{
    if (m_sessions.isEmpty()) {
        // We are not initialized yet, so do that now
        QDir sessionDir(Core::ICore::userResourcePath());
        QList<QFileInfo> sessionFiles = sessionDir.entryInfoList(QStringList() << QLatin1String("*.qws"), QDir::NoFilter, QDir::Time);
        Q_FOREACH(const QFileInfo& fileInfo, sessionFiles) {
            if (fileInfo.completeBaseName() != QLatin1String("default"))
                m_sessions << fileInfo.completeBaseName();
        }
        m_sessions.prepend(QLatin1String("default"));
    }
    return m_sessions;
}

QString SessionManager::sessionNameToFileName(const QString &session) const
{
    return ICore::userResourcePath() + QLatin1Char('/') + session + QLatin1String(".qws");
}

/*!
    \brief Just creates a new session (Does not actually create the file).
*/

bool SessionManager::createSession(const QString &session)
{
    if (sessions().contains(session))
        return false;
    Q_ASSERT(m_sessions.size() > 0);
    m_sessions.insert(1, session);
    return true;
}

bool SessionManager::renameSession(const QString &original, const QString &newName)
{
    if (!cloneSession(original, newName))
        return false;
    if (original == activeSession())
        loadSession(newName);
    return deleteSession(original);
}

/*!
     \brief Deletes session name from session list and file from disk.
*/
bool SessionManager::deleteSession(const QString &session)
{
    if (!m_sessions.contains(session))
        return false;
    m_sessions.removeOne(session);
    QFile fi(sessionNameToFileName(session));
    if (fi.exists())
        return fi.remove();
    return false;
}

bool SessionManager::cloneSession(const QString &original, const QString &clone)
{
    if (!m_sessions.contains(original))
        return false;

    QFile fi(sessionNameToFileName(original));
    // If the file does not exist, we can still clone
    if (!fi.exists() || fi.copy(sessionNameToFileName(clone))) {
        Q_ASSERT(m_sessions.size() > 0);
        m_sessions.insert(1, clone);
        return true;
    }
    return false;
}

void SessionManager::restoreValues(const Utils::PersistentSettingsReader &reader)
{
    const QStringList &keys = reader.restoreValue(QLatin1String("valueKeys")).toStringList();
    foreach (const QString &key, keys) {
        QVariant value = reader.restoreValue(QLatin1String("value-") + key);
        m_values.insert(key, value);
    }
}

void SessionManager::restoreDependencies(const Utils::PersistentSettingsReader &reader)
{
    QMap<QString, QVariant> depMap = reader.restoreValue(QLatin1String("ProjectDependencies")).toMap();
    QMap<QString, QVariant>::const_iterator i = depMap.constBegin();
    while (i != depMap.constEnd()) {
        const QString &key = i.key();
        if (m_failedProjects.contains(key))
            continue;
        QStringList values;
        foreach (const QString &value, i.value().toStringList()) {
            if (!m_failedProjects.contains(value))
                values << value;
        }
        m_depMap.insert(key, values);
        ++i;
    }
}

void SessionManager::askUserAboutFailedProjects()
{
    QStringList failedProjects = m_failedProjects;
    if (!failedProjects.isEmpty()) {
        QString fileList =
            QDir::toNativeSeparators(failedProjects.join(QLatin1String("<br>")));
        QMessageBox * box = new QMessageBox(QMessageBox::Warning,
                                            tr("Failed to restore project files"),
                                            tr("Could not restore the following project files:<br><b>%1</b>").
                                            arg(fileList));
        QPushButton * keepButton = new QPushButton(tr("Keep projects in Session"), box);
        QPushButton * removeButton = new QPushButton(tr("Remove projects from Session"), box);
        box->addButton(keepButton, QMessageBox::AcceptRole);
        box->addButton(removeButton, QMessageBox::DestructiveRole);

        box->exec();

        if (box->clickedButton() == removeButton)
            m_failedProjects.clear();
    }
}

void SessionManager::restoreStartupProject(const Utils::PersistentSettingsReader &reader)
{
    const QString startupProject = reader.restoreValue(QLatin1String("StartupProject")).toString();
    if (!startupProject.isEmpty()) {
        const QString startupProjectPath = startupProject;
        foreach (Project *pro, m_projects) {
            if (QDir::cleanPath(pro->document()->fileName()) == startupProjectPath) {
                setStartupProject(m_startupProject);
                break;
            }
        }
        if (!m_startupProject)
            qWarning() << "Could not find startup project" << startupProjectPath;
    }
}

void SessionManager::restoreEditors(const Utils::PersistentSettingsReader &reader)
{
    const QVariant &editorsettings = reader.restoreValue(QLatin1String("EditorSettings"));
    if (editorsettings.isValid()) {
        connect(ICore::editorManager(), SIGNAL(editorOpened(Core::IEditor *)),
                this, SLOT(sessionLoadingProgress()));
        ICore::editorManager()->restoreState(
            QByteArray::fromBase64(editorsettings.toByteArray()));
        disconnect(ICore::editorManager(), SIGNAL(editorOpened(Core::IEditor *)),
                   this, SLOT(sessionLoadingProgress()));
    }
}

/*!
     \brief Loads a session, takes a session name (not filename).
*/
void SessionManager::restoreProjects(const QStringList &fileList)
{
    // indirectly adds projects to session
    // Keep projects that failed to load in the session!
    m_failedProjects = fileList;
    if (!fileList.isEmpty()) {
        QString errors;
        QList<Project *> projects = ProjectExplorerPlugin::instance()->openProjects(fileList, &errors);
        if (!errors.isEmpty())
            QMessageBox::critical(Core::ICore::mainWindow(), tr("Failed to open project"), errors);
        foreach (Project *p, projects)
            m_failedProjects.removeAll(p->document()->fileName());
    }
}

bool SessionManager::loadSession(const QString &session)
{
    // Do nothing if we have that session already loaded,
    // exception if the session is the default virgin session
    // we still want to be able to load the default session
    if (session == m_sessionName && !isDefaultVirgin())
        return true;

    if (!sessions().contains(session))
        return false;

    // Try loading the file
    QString fileName = sessionNameToFileName(session);
    PersistentSettingsReader reader;
    if (QFileInfo(fileName).exists()) {
        if (!reader.load(fileName)) {
            QMessageBox::warning(0, tr("Error while restoring session"),
                                 tr("Could not restore session %1").arg(fileName));
            return false;
        }
    }

    // Allow everyone to set something in the session and before saving
    emit aboutToUnloadSession(m_sessionName);

    if (!isDefaultVirgin()) {
        if (!save())
            return false;
    }

    // Clean up
    if (!ICore::editorManager()->closeAllEditors())
        return false;

    setStartupProject(0);
    removeProjects(projects());

    m_failedProjects.clear();
    m_depMap.clear();
    m_values.clear();

    m_sessionName = session;
    updateWindowTitle();

    if (QFileInfo(fileName).exists()) {
        m_virginSession = false;

        ICore::progressManager()->addTask(m_future.future(), tr("Session"),
           QLatin1String("ProjectExplorer.SessionFile.Load"));

        restoreValues(reader);
        emit aboutToLoadSession(session);

        QStringList fileList =
            reader.restoreValue(QLatin1String("ProjectList")).toStringList();
        int openEditorsCount = reader.restoreValue(QLatin1String("OpenEditors")).toInt();

        m_future.setProgressRange(0, fileList.count() + openEditorsCount + 2);
        m_future.setProgressValue(1);

        restoreProjects(fileList);
        sessionLoadingProgress();
        restoreDependencies(reader);
        restoreStartupProject(reader);
        restoreEditors(reader);

        m_future.reportFinished();
        m_future = QFutureInterface<void>();

        // restore the active mode
        QString modeIdentifier = value(QLatin1String("ActiveMode")).toString();
        if (modeIdentifier.isEmpty())
            modeIdentifier = QLatin1String(Core::Constants::MODE_EDIT);

        ModeManager::activateMode(modeIdentifier);
        ModeManager::setFocusToCurrentMode();
    } else {
        ModeManager::activateMode(QLatin1String(Core::Constants::MODE_EDIT));
        ModeManager::setFocusToCurrentMode();
    }
    emit sessionLoaded(session);

    // Starts a event loop, better do that at the very end
    askUserAboutFailedProjects();
    return true;
}

QString SessionManager::lastSession() const
{
    QString fileName = ICore::settings()->value(QLatin1String("ProjectExplorer/StartupSession")).toString();
    return QFileInfo(fileName).completeBaseName();
}

SessionNode *SessionManager::sessionNode() const
{
    return m_sessionNode;
}

void SessionManager::reportProjectLoadingProgress()
{
    sessionLoadingProgress();
}

void SessionManager::markSessionFileDirty(bool makeDefaultVirginDirty)
{
    m_autoSaveSessionTimer->start();
    if (makeDefaultVirginDirty)
        m_virginSession = false;
}

void SessionManager::sessionLoadingProgress()
{
    m_future.setProgressValue(m_future.progressValue() + 1);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

QStringList ProjectExplorer::SessionManager::projectsForSessionName(const QString &session) const
{
    const QString fileName = sessionNameToFileName(session);
    PersistentSettingsReader reader;
    if (QFileInfo(fileName).exists()) {
        if (!reader.load(fileName)) {
            qWarning() << "Could not restore session" << fileName;
            return QStringList();
        }
    }
    return reader.restoreValue(QLatin1String("ProjectList")).toStringList();
}
