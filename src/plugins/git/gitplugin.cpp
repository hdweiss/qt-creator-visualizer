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

#include "gitplugin.h"

#include "changeselectiondialog.h"
#include "commitdata.h"
#include "gitclient.h"
#include "gitconstants.h"
#include "giteditor.h"
#include "gitsubmiteditor.h"
#include "gitversioncontrol.h"
#include "branchdialog.h"
#include "remotedialog.h"
#include "clonewizard.h"
#include "gitoriousclonewizard.h"
#include "stashdialog.h"
#include "settingspage.h"

#include <coreplugin/icore.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/id.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/documentmanager.h>

#include <utils/qtcassert.h>
#include <utils/parameteraction.h>
#include <utils/fileutils.h>

#include <vcsbase/basevcseditorfactory.h>
#include <vcsbase/submitfilemodel.h>
#include <vcsbase/vcsbaseeditor.h>
#include <vcsbase/basevcssubmiteditorfactory.h>
#include <vcsbase/vcsbaseoutputwindow.h>
#include <vcsbase/cleandialog.h>
#include <locator/commandlocator.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QtPlugin>

#include <QAction>
#include <QFileDialog>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>

static const VcsBase::VcsBaseEditorParameters editorParameters[] = {
{
    VcsBase::RegularCommandOutput,
    Git::Constants::GIT_COMMAND_LOG_EDITOR_ID,
    Git::Constants::GIT_COMMAND_LOG_EDITOR_DISPLAY_NAME,
    Git::Constants::C_GIT_COMMAND_LOG_EDITOR,
    "application/vnd.nokia.text.scs_git_commandlog",
    "gitlog"},
{   VcsBase::LogOutput,
    Git::Constants::GIT_LOG_EDITOR_ID,
    Git::Constants::GIT_LOG_EDITOR_DISPLAY_NAME,
    Git::Constants::C_GIT_LOG_EDITOR,
    "application/vnd.nokia.text.scs_git_filelog",
    "gitfilelog"},
{   VcsBase::AnnotateOutput,
    Git::Constants::GIT_BLAME_EDITOR_ID,
    Git::Constants::GIT_BLAME_EDITOR_DISPLAY_NAME,
    Git::Constants::C_GIT_BLAME_EDITOR,
    "application/vnd.nokia.text.scs_git_annotation",
    "gitsannotate"},
{   VcsBase::DiffOutput,
    Git::Constants::GIT_DIFF_EDITOR_ID,
    Git::Constants::GIT_DIFF_EDITOR_DISPLAY_NAME,
    Git::Constants::C_GIT_DIFF_EDITOR,
    "text/x-patch","diff"}
};

// Utility to find a parameter set by type
static inline const VcsBase::VcsBaseEditorParameters *findType(int ie)
{
    const VcsBase::EditorContentType et = static_cast<VcsBase::EditorContentType>(ie);
    return  VcsBase::VcsBaseEditorWidget::findType(editorParameters, sizeof(editorParameters)/sizeof(VcsBase::VcsBaseEditorParameters), et);
}

Q_DECLARE_METATYPE(Git::Internal::GitClientMemberFunc)

using namespace Git;
using namespace Git::Internal;

// GitPlugin

GitPlugin *GitPlugin::m_instance = 0;

GitPlugin::GitPlugin() :
    VcsBase::VcsBasePlugin(QLatin1String(Git::Constants::GITSUBMITEDITOR_ID)),
    m_commandLocator(0),
    m_showAction(0),
    m_submitCurrentAction(0),
    m_diffSelectedFilesAction(0),
    m_undoAction(0),
    m_redoAction(0),
    m_menuAction(0),
    m_applyCurrentFilePatchAction(0),
    m_gitClient(0),
    m_changeSelectionDialog(0),
    m_submitActionTriggered(false)
{
    m_instance = this;
    const int mid = qRegisterMetaType<GitClientMemberFunc>();
    Q_UNUSED(mid)
    m_fileActions.reserve(10);
    m_projectActions.reserve(10);
    m_repositoryActions.reserve(15);
}

GitPlugin::~GitPlugin()
{
    cleanCommitMessageFile();
    delete m_gitClient;
    m_instance = 0;
}

void GitPlugin::cleanCommitMessageFile()
{
    if (!m_commitMessageFileName.isEmpty()) {
        QFile::remove(m_commitMessageFileName);
        m_commitMessageFileName.clear();
    }
}

bool GitPlugin::isCommitEditorOpen() const
{
    return !m_commitMessageFileName.isEmpty();
}

GitPlugin *GitPlugin::instance()
{
    return m_instance;
}

static const VcsBase::VcsBaseSubmitEditorParameters submitParameters = {
    Git::Constants::SUBMIT_MIMETYPE,
    Git::Constants::GITSUBMITEDITOR_ID,
    Git::Constants::GITSUBMITEDITOR_DISPLAY_NAME,
    Git::Constants::C_GITSUBMITEDITOR
};

static Core::Command *createSeparator(Core::ActionManager *am,
                                       const Core::Context &context,
                                       const Core::Id &id,
                                       QObject *parent)
{
    QAction *a = new QAction(parent);
    a->setSeparator(true);
    return am->registerAction(a, id, context);
}

// Create a parameter action
ParameterActionCommandPair
        GitPlugin::createParameterAction(Core::ActionManager *am, Core::ActionContainer *ac,
                                         const QString &defaultText, const QString &parameterText,
                                         const Core::Id &id, const Core::Context &context,
                                         bool addToLocator)
{
    Utils::ParameterAction *action = new Utils::ParameterAction(defaultText, parameterText,
                                                                Utils::ParameterAction::EnabledWithParameter,
                                                                this);
    Core::Command *command = am->registerAction(action, id, context);
    command->setAttribute(Core::Command::CA_UpdateText);
    ac->addAction(command);
    if (addToLocator)
        m_commandLocator->appendCommand(command);
    return ParameterActionCommandPair(action, command);
}

// Create an action to act on a file with a slot.
ParameterActionCommandPair
        GitPlugin::createFileAction(Core::ActionManager *am, Core::ActionContainer *ac,
                                    const QString &defaultText, const QString &parameterText,
                                    const Core::Id &id, const Core::Context &context, bool addToLocator,
                                    const char *pluginSlot)
{
    const ParameterActionCommandPair rc = createParameterAction(am, ac, defaultText, parameterText, id, context, addToLocator);
    m_fileActions.push_back(rc.first);
    connect(rc.first, SIGNAL(triggered()), this, pluginSlot);
    return rc;
}

// Create an action to act on a project with slot.
ParameterActionCommandPair
        GitPlugin::createProjectAction(Core::ActionManager *am, Core::ActionContainer *ac,
                                       const QString &defaultText, const QString &parameterText,
                                       const Core::Id &id, const Core::Context &context, bool addToLocator,
                                       const char *pluginSlot)
{
    const ParameterActionCommandPair rc = createParameterAction(am, ac, defaultText, parameterText, id, context, addToLocator);
    m_projectActions.push_back(rc.first);
    connect(rc.first, SIGNAL(triggered()), this, pluginSlot);
    return rc;
}

// Create an action to act on the repository
ActionCommandPair
        GitPlugin::createRepositoryAction(Core::ActionManager *am, Core::ActionContainer *ac,
                                          const QString &text, const Core::Id &id,
                                          const Core::Context &context, bool addToLocator)
{
    QAction  *action = new QAction(text, this);
    Core::Command *command = am->registerAction(action, id, context);
    ac->addAction(command);
    m_repositoryActions.push_back(action);
    if (addToLocator)
        m_commandLocator->appendCommand(command);
    return ActionCommandPair(action, command);
}

// Create an action to act on the repository with slot
ActionCommandPair
        GitPlugin::createRepositoryAction(Core::ActionManager *am, Core::ActionContainer *ac,
                                          const QString &text, const Core::Id &id,
                                          const Core::Context &context, bool addToLocator,
                                          const char *pluginSlot)
{
    const ActionCommandPair rc = createRepositoryAction(am, ac, text, id, context, addToLocator);
    connect(rc.first, SIGNAL(triggered()), this, pluginSlot);
    return rc;
}

// Action to act on the repository forwarded to a git client member function
// taking the directory. Store the member function as data on the action.
ActionCommandPair
        GitPlugin::createRepositoryAction(Core::ActionManager *am, Core::ActionContainer *ac,
                                          const QString &text, const Core::Id &id,
                                          const Core::Context &context, bool addToLocator,
                                          GitClientMemberFunc func)
{
    // Set the member func as data and connect to generic slot
    const ActionCommandPair rc = createRepositoryAction(am, ac, text, id, context, addToLocator);
    rc.first->setData(qVariantFromValue(func));
    connect(rc.first, SIGNAL(triggered()), this, SLOT(gitClientMemberFuncRepositoryAction()));
    return rc;
}

bool GitPlugin::initialize(const QStringList &arguments, QString *errorMessage)
{
    Q_UNUSED(arguments)
    Q_UNUSED(errorMessage)

    m_settings.readSettings(Core::ICore::settings());

    m_gitClient = new GitClient(&m_settings);

    typedef VcsBase::VcsEditorFactory<GitEditor> GitEditorFactory;
    typedef VcsBase::VcsSubmitEditorFactory<GitSubmitEditor> GitSubmitEditorFactory;

    initializeVcs(new GitVersionControl(m_gitClient));

    // Create the globalcontext list to register actions accordingly
    Core::Context globalcontext(Core::Constants::C_GLOBAL);

    // Create the settings Page
    addAutoReleasedObject(new SettingsPage());

    static const char *describeSlot = SLOT(show(QString,QString));
    const int editorCount = sizeof(editorParameters)/sizeof(VcsBase::VcsBaseEditorParameters);
    for (int i = 0; i < editorCount; i++)
        addAutoReleasedObject(new GitEditorFactory(editorParameters + i, m_gitClient, describeSlot));

    addAutoReleasedObject(new GitSubmitEditorFactory(&submitParameters));
    addAutoReleasedObject(new CloneWizard);
    addAutoReleasedObject(new Gitorious::Internal::GitoriousCloneWizard);

    const QString description = QLatin1String("Git");
    const QString prefix = QLatin1String("git");
    m_commandLocator = new Locator::CommandLocator(description, prefix, prefix);
    addAutoReleasedObject(m_commandLocator);

    //register actions
    Core::ActionManager *actionManager = Core::ICore::actionManager();

    Core::ActionContainer *toolsContainer =
        actionManager->actionContainer(Core::Constants::M_TOOLS);

    Core::ActionContainer *gitContainer = actionManager->createMenu("Git");
    gitContainer->menu()->setTitle(tr("&Git"));
    toolsContainer->addMenu(gitContainer);
    m_menuAction = gitContainer->menu()->menuAction();

    ParameterActionCommandPair parameterActionCommand
            = createFileAction(actionManager, gitContainer,
                               tr("Blame Current File"), tr("Blame for \"%1\""),
                               Core::Id("Git.Blame"),
                               globalcontext, true, SLOT(blameFile()));
    parameterActionCommand.second->setDefaultKeySequence(QKeySequence(tr("Alt+G,Alt+B")));

    parameterActionCommand
            = createFileAction(actionManager, gitContainer,
                               tr("Diff Current File"), tr("Diff of \"%1\""),
                               Core::Id("Git.Diff"), globalcontext, true,
                               SLOT(diffCurrentFile()));
    parameterActionCommand.second->setDefaultKeySequence(QKeySequence(tr("Alt+G,Alt+D")));

    parameterActionCommand
            = createFileAction(actionManager, gitContainer,
                               tr("Log Current File"), tr("Log of \"%1\""),
                               Core::Id("Git.Log"), globalcontext, true, SLOT(logFile()));
    parameterActionCommand.second->setDefaultKeySequence(QKeySequence(tr("Alt+G,Alt+L")));


    // ------
    gitContainer->addAction(createSeparator(actionManager, globalcontext, Core::Id("Git.Sep.File"), this));

    parameterActionCommand
            = createFileAction(actionManager, gitContainer,
                               tr("Stage File for Commit"), tr("Stage \"%1\" for Commit"),
                               Core::Id("Git.Stage"), globalcontext, true, SLOT(stageFile()));
    parameterActionCommand.second->setDefaultKeySequence(QKeySequence(tr("Alt+G,Alt+A")));

    parameterActionCommand
            = createFileAction(actionManager, gitContainer,
                               tr("Unstage File from Commit"), tr("Unstage \"%1\" from Commit"),
                               Core::Id("Git.Unstage"), globalcontext, true, SLOT(unstageFile()));

    parameterActionCommand
            = createFileAction(actionManager, gitContainer,
                               tr("Undo Unstaged Changes"), tr("Undo Unstaged Changes for \"%1\""),
                               Core::Id("Git.UndoUnstaged"), globalcontext,
                               true, SLOT(undoUnstagedFileChanges()));

    parameterActionCommand
            = createFileAction(actionManager, gitContainer,
                               tr("Undo Uncommitted Changes"), tr("Undo Uncommitted Changes for \"%1\""),
                               Core::Id("Git.Undo"), globalcontext,
                               true, SLOT(undoFileChanges()));
    parameterActionCommand.second->setDefaultKeySequence(QKeySequence(tr("Alt+G,Alt+U")));


    // ------------
    gitContainer->addAction(createSeparator(actionManager, globalcontext, Core::Id("Git.Sep.Project"), this));

    parameterActionCommand
            = createProjectAction(actionManager, gitContainer,
                                  tr("Diff Current Project"), tr("Diff Project \"%1\""),
                                  Core::Id("Git.DiffProject"),
                                  globalcontext, true,
                                  SLOT(diffCurrentProject()));
    parameterActionCommand.second->setDefaultKeySequence(QKeySequence(tr("Alt+G,Alt+Shift+D")));

    parameterActionCommand
            = createProjectAction(actionManager, gitContainer,
                                  tr("Log Project"), tr("Log Project \"%1\""),
                                  Core::Id("Git.LogProject"), globalcontext, true,
                                  SLOT(logProject()));
    parameterActionCommand.second->setDefaultKeySequence(QKeySequence(tr("Alt+G,Alt+K")));

    parameterActionCommand
                = createProjectAction(actionManager, gitContainer,
                                      tr("Clean Project..."), tr("Clean Project \"%1\"..."),
                                      Core::Id("Git.CleanProject"), globalcontext,
                                      true, SLOT(cleanProject()));


    // --------------
    gitContainer->addAction(createSeparator(actionManager, globalcontext, Core::Id("Git.Sep.Repository"), this));

    createRepositoryAction(actionManager, gitContainer,
                           tr("Diff"), Core::Id("Git.DiffRepository"),
                           globalcontext, true, SLOT(diffRepository()));

    createRepositoryAction(actionManager, gitContainer,
                           tr("Log"), Core::Id("Git.LogRepository"),
                           globalcontext, true, &GitClient::graphLog);

    createRepositoryAction(actionManager, gitContainer,
                           tr("Status"), Core::Id("Git.StatusRepository"),
                           globalcontext, true, &GitClient::status);

    createRepositoryAction(actionManager, gitContainer,
                           tr("Undo Uncommitted Changes..."), Core::Id("Git.UndoRepository"),
                           globalcontext, false, SLOT(undoRepositoryChanges()));


    createRepositoryAction(actionManager, gitContainer,
                           tr("Clean..."), Core::Id("Git.CleanRepository"),
                           globalcontext, true, SLOT(cleanRepository()));

    m_createRepositoryAction = new QAction(tr("Create Repository..."), this);
    Core::Command *createRepositoryCommand = actionManager->registerAction(m_createRepositoryAction, "Git.CreateRepository", globalcontext);
    connect(m_createRepositoryAction, SIGNAL(triggered()), this, SLOT(createRepository()));
    gitContainer->addAction(createRepositoryCommand);

    // --------------
    gitContainer->addAction(createSeparator(actionManager, globalcontext, Core::Id("Git.Sep.Info"), this));

    createRepositoryAction(actionManager, gitContainer,
                           tr("Launch gitk"), Core::Id("Git.LaunchGitK"),
                           globalcontext, true, &GitClient::launchGitK);

    createRepositoryAction(actionManager, gitContainer,
                           tr("Branches..."), Core::Id("Git.BranchList"),
                           globalcontext, true, SLOT(branchList()));

    createRepositoryAction(actionManager, gitContainer,
                           tr("Remotes..."), Core::Id("Git.RemoteList"),
                           globalcontext, false, SLOT(remoteList()));

    m_showAction = new QAction(tr("Show..."), this);
    Core::Command *showCommitCommand = actionManager->registerAction(m_showAction, "Git.ShowCommit", globalcontext);
    connect(m_showAction, SIGNAL(triggered()), this, SLOT(showCommit()));
    gitContainer->addAction(showCommitCommand);


    // --------------
    gitContainer->addAction(createSeparator(actionManager, globalcontext, Core::Id("Git.Sep.RarelyUsed"), this));

    Core::ActionContainer *patchMenu = actionManager->createMenu(Core::Id("Git.PatchMenu"));
    patchMenu->menu()->setTitle(tr("Patch"));
    gitContainer->addMenu(patchMenu);

    // Apply current file as patch is handled specially.
    parameterActionCommand =
            createParameterAction(actionManager, patchMenu,
                                  tr("Apply from Editor"), tr("Apply \"%1\""),
                                  Core::Id("Git.ApplyCurrentFilePatch"),
                                  globalcontext, true);
    m_applyCurrentFilePatchAction = parameterActionCommand.first;
    connect(m_applyCurrentFilePatchAction, SIGNAL(triggered()), this,
            SLOT(applyCurrentFilePatch()));

    createRepositoryAction(actionManager, patchMenu,
                           tr("Apply from File..."), Core::Id("Git.ApplyPatch"),
                           globalcontext, true, SLOT(promptApplyPatch()));

    Core::ActionContainer *stashMenu = actionManager->createMenu(Core::Id("Git.StashMenu"));
    stashMenu->menu()->setTitle(tr("Stash"));
    gitContainer->addMenu(stashMenu);

    createRepositoryAction(actionManager, stashMenu,
                           tr("Stashes..."), Core::Id("Git.StashList"),
                           globalcontext, false, SLOT(stashList()));

    stashMenu->addAction(createSeparator(actionManager, globalcontext, Core::Id("Git.Sep.StashMenuPush"), this));

    ActionCommandPair actionCommand =
            createRepositoryAction(actionManager, stashMenu,
                                   tr("Stash"), Core::Id("Git.Stash"),
                                   globalcontext, true, SLOT(stash()));
    actionCommand.first->setToolTip(tr("Saves the current state of your work and resets the repository."));

    actionCommand = createRepositoryAction(actionManager, stashMenu,
                                           tr("Take Snapshot..."), Core::Id("Git.StashSnapshot"),
                                           globalcontext, true, SLOT(stashSnapshot()));
    actionCommand.first->setToolTip(tr("Saves the current state of your work."));

    stashMenu->addAction(createSeparator(actionManager, globalcontext, Core::Id("Git.Sep.StashMenuPop"), this));

    actionCommand = createRepositoryAction(actionManager, stashMenu,
                                           tr("Stash Pop"), Core::Id("Git.StashPop"),
                                           globalcontext, true, &GitClient::stashPop);
    actionCommand.first->setToolTip(tr("Restores changes saved to the stash list using \"Stash\"."));

    Core::ActionContainer *subversionMenu = actionManager->createMenu(Core::Id("Git.Subversion"));
    subversionMenu->menu()->setTitle(tr("Subversion"));
    gitContainer->addMenu(subversionMenu);

    createRepositoryAction(actionManager, subversionMenu,
                           tr("Log"), Core::Id("Git.Subversion.Log"),
                           globalcontext, false, &GitClient::subversionLog);

    createRepositoryAction(actionManager, subversionMenu,
                           tr("Fetch"), Core::Id("Git.Subversion.Fetch"),
                           globalcontext, false, &GitClient::synchronousSubversionFetch);

    gitContainer->addAction(createSeparator(actionManager, globalcontext, Core::Id("Git.Sep.PushPull"), this));

    gitContainer->addAction(createSeparator(actionManager, globalcontext, Core::Id("Git.Sep.Global"), this));

    createRepositoryAction(actionManager, gitContainer,
                           tr("Fetch"), Core::Id("Git.Fetch"),
                           globalcontext, true, SLOT(fetch()));

    createRepositoryAction(actionManager, gitContainer,
                           tr("Pull"), Core::Id("Git.Pull"),
                           globalcontext, true, SLOT(pull()));

    actionCommand = createRepositoryAction(actionManager, gitContainer,
                                           tr("Push"), Core::Id("Git.Push"),
                                           globalcontext, true, SLOT(push()));

    actionCommand = createRepositoryAction(actionManager, gitContainer,
                                           tr("Commit..."), Core::Id("Git.Commit"),
                                           globalcontext, true, SLOT(startCommit()));
    actionCommand.second->setDefaultKeySequence(QKeySequence(tr("Alt+G,Alt+C")));

    createRepositoryAction(actionManager, gitContainer,
                           tr("Amend Last Commit..."), Core::Id("Git.AmendCommit"),
                           globalcontext, true, SLOT(startAmendCommit()));

    // Subversion in a submenu.
    gitContainer->addAction(createSeparator(actionManager, globalcontext, Core::Id("Git.Sep.Subversion"), this));

    if (0) {
        const QList<QAction*> snapShotActions = createSnapShotTestActions();
        const int count = snapShotActions.size();
        for (int i = 0; i < count; i++) {
            Core::Command *tCommand
                    = actionManager->registerAction(snapShotActions.at(i),
                                                    Core::Id(QLatin1String("Git.Snapshot.") + QString::number(i)),
                                                    globalcontext);
            gitContainer->addAction(tCommand);
        }
    }

    // Submit editor
    Core::Context submitContext(Constants::C_GITSUBMITEDITOR);
    m_submitCurrentAction = new QAction(VcsBase::VcsBaseSubmitEditor::submitIcon(), tr("Commit"), this);
    Core::Command *command = actionManager->registerAction(m_submitCurrentAction, Constants::SUBMIT_CURRENT, submitContext);
    command->setAttribute(Core::Command::CA_UpdateText);
    connect(m_submitCurrentAction, SIGNAL(triggered()), this, SLOT(submitCurrentLog()));

    m_diffSelectedFilesAction = new QAction(VcsBase::VcsBaseSubmitEditor::diffIcon(), tr("Diff &Selected Files"), this);
    command = actionManager->registerAction(m_diffSelectedFilesAction, Constants::DIFF_SELECTED, submitContext);

    m_undoAction = new QAction(tr("&Undo"), this);
    command = actionManager->registerAction(m_undoAction, Core::Constants::UNDO, submitContext);

    m_redoAction = new QAction(tr("&Redo"), this);
    command = actionManager->registerAction(m_redoAction, Core::Constants::REDO, submitContext);

    return true;
}

GitVersionControl *GitPlugin::gitVersionControl() const
{
    return static_cast<GitVersionControl *>(versionControl());
}

void GitPlugin::submitEditorDiff(const QStringList &unstaged, const QStringList &staged)
{
    m_gitClient->diff(m_submitRepository, QStringList(), unstaged, staged);
}

void GitPlugin::diffCurrentFile()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasFile(), return)
    m_gitClient->diff(state.currentFileTopLevel(), QStringList(), state.relativeCurrentFile());
}

void GitPlugin::diffCurrentProject()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasProject(), return)
    m_gitClient->diff(state.currentProjectTopLevel(), QStringList(), state.relativeCurrentProject());
}

void GitPlugin::diffRepository()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasTopLevel(), return)
    m_gitClient->diff(state.topLevel(), QStringList(), QStringList());
}

void GitPlugin::logFile()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasFile(), return)
    m_gitClient->log(state.currentFileTopLevel(), QStringList(state.relativeCurrentFile()), true);
}

void GitPlugin::blameFile()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasFile(), return)
    const int lineNumber = VcsBase::VcsBaseEditorWidget::lineNumberOfCurrentEditor(state.currentFile());
    m_gitClient->blame(state.currentFileTopLevel(), QStringList(), state.relativeCurrentFile(), QString(), lineNumber);
}

void GitPlugin::logProject()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasProject(), return)
    m_gitClient->log(state.currentProjectTopLevel(), state.relativeCurrentProject());
}

void GitPlugin::undoFileChanges(bool revertStaging)
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasFile(), return)
    Core::FileChangeBlocker fcb(state.currentFile());
    m_gitClient->revert(QStringList(state.currentFile()), revertStaging);
}

void GitPlugin::undoUnstagedFileChanges()
{
    undoFileChanges(false);
}

void GitPlugin::undoRepositoryChanges()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasTopLevel(), return)
    const QString msg = tr("Undo all pending changes to the repository\n%1?").arg(QDir::toNativeSeparators(state.topLevel()));
    const QMessageBox::StandardButton answer
            = QMessageBox::question(Core::ICore::mainWindow(),
                                    tr("Undo Changes"), msg,
                                    QMessageBox::Yes|QMessageBox::No,
                                    QMessageBox::No);
    if (answer == QMessageBox::No)
        return;
    m_gitClient->hardReset(state.topLevel(), QString());
}

void GitPlugin::stageFile()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasFile(), return)
    m_gitClient->addFile(state.currentFileTopLevel(), state.relativeCurrentFile());
}

void GitPlugin::unstageFile()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasFile(), return)
    m_gitClient->synchronousReset(state.currentFileTopLevel(), QStringList(state.relativeCurrentFile()));
}

void GitPlugin::startAmendCommit()
{
    startCommit(true);
}

void GitPlugin::startCommit()
{
    startCommit(false);
}

void GitPlugin::startCommit(bool amend)
{
    if (VcsBase::VcsBaseSubmitEditor::raiseSubmitEditor())
        return;
    if (isCommitEditorOpen()) {
        VcsBase::VcsBaseOutputWindow::instance()->appendWarning(tr("Another submit is currently being executed."));
        return;
    }

    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasTopLevel(), return)

    QString errorMessage, commitTemplate;
    CommitData data;
    if (!m_gitClient->getCommitData(state.topLevel(), amend, &commitTemplate, &data, &errorMessage)) {
        VcsBase::VcsBaseOutputWindow::instance()->append(errorMessage);
        return;
    }

    // Store repository for diff and the original list of
    // files to be able to unstage files the user unchecks
    m_submitRepository = data.panelInfo.repository;
    m_commitAmendSHA1 = data.amendSHA1;

    // Start new temp file with message template
    Utils::TempFileSaver saver;
    // Keep the file alive, else it removes self and forgets its name
    saver.setAutoRemove(false);
    saver.write(commitTemplate.toLocal8Bit());
    if (!saver.finalize()) {
        VcsBase::VcsBaseOutputWindow::instance()->append(saver.errorString());
        return;
    }
    m_commitMessageFileName = saver.fileName();
    openSubmitEditor(m_commitMessageFileName, data, amend);
}

Core::IEditor *GitPlugin::openSubmitEditor(const QString &fileName, const CommitData &cd, bool amend)
{
    Core::IEditor *editor = Core::ICore::editorManager()->openEditor(fileName, Constants::GITSUBMITEDITOR_ID,
                                                                Core::EditorManager::ModeSwitch);
    GitSubmitEditor *submitEditor = qobject_cast<GitSubmitEditor*>(editor);
    QTC_ASSERT(submitEditor, return 0);
    // The actions are for some reason enabled by the context switching
    // mechanism. Disable them correctly.
    submitEditor->registerActions(m_undoAction, m_redoAction, m_submitCurrentAction, m_diffSelectedFilesAction);
    submitEditor->setCommitData(cd);
    submitEditor->setCheckScriptWorkingDirectory(m_submitRepository);
    const QString title = amend ? tr("Amend %1").arg(cd.amendSHA1) : tr("Git Commit");
    submitEditor->setDisplayName(title);
    if (amend) // Allow for just correcting the message
        submitEditor->setEmptyFileListEnabled(true);
    connect(submitEditor, SIGNAL(diff(QStringList,QStringList)), this, SLOT(submitEditorDiff(QStringList,QStringList)));
    return editor;
}

void GitPlugin::submitCurrentLog()
{
    // Close the submit editor
    m_submitActionTriggered = true;
    QList<Core::IEditor*> editors;
    editors.push_back(Core::ICore::editorManager()->currentEditor());
    Core::ICore::editorManager()->closeEditors(editors);
}

bool GitPlugin::submitEditorAboutToClose(VcsBase::VcsBaseSubmitEditor *submitEditor)
{
    if (!isCommitEditorOpen())
        return false;
    Core::IDocument *editorDocument = submitEditor->document();
    const GitSubmitEditor *editor = qobject_cast<GitSubmitEditor *>(submitEditor);
    if (!editorDocument || !editor)
        return true;
    // Submit editor closing. Make it write out the commit message
    // and retrieve files
    const QFileInfo editorFile(editorDocument->fileName());
    const QFileInfo changeFile(m_commitMessageFileName);
    // Paranoia!
    if (editorFile.absoluteFilePath() != changeFile.absoluteFilePath())
        return true;
    // Prompt user. Force a prompt unless submit was actually invoked (that
    // is, the editor was closed or shutdown).
    bool *promptData = m_settings.boolPointer(GitSettings::promptOnSubmitKey);
    const VcsBase::VcsBaseSubmitEditor::PromptSubmitResult answer =
            editor->promptSubmit(tr("Closing Git Editor"),
                                 tr("Do you want to commit the change?"),
                                 tr("Git will not accept this commit. Do you want to continue to edit it?"),
                                 promptData, !m_submitActionTriggered, false);
    m_submitActionTriggered = false;
    switch (answer) {
    case VcsBase::VcsBaseSubmitEditor::SubmitCanceled:
        return false; // Keep editing and change file
    case VcsBase::VcsBaseSubmitEditor::SubmitDiscarded:
        cleanCommitMessageFile();
        return true; // Cancel all
    default:
        break;
    }


    // Go ahead!
    VcsBase::SubmitFileModel *model = qobject_cast<VcsBase::SubmitFileModel *>(editor->fileModel());
    bool closeEditor = true;
    if (model->hasCheckedFiles() || !m_commitAmendSHA1.isEmpty()) {
        // get message & commit
        if (!Core::DocumentManager::saveDocument(editorDocument))
            return false;

        closeEditor = m_gitClient->addAndCommit(m_submitRepository, editor->panelData(),
                                                m_commitAmendSHA1, m_commitMessageFileName, model);
    }
    if (closeEditor)
        cleanCommitMessageFile();
    return closeEditor;
}

void GitPlugin::fetch()
{
    m_gitClient->synchronousFetch(currentState().topLevel(), QString());
}

void GitPlugin::pull()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasTopLevel(), return)

    switch (m_gitClient->ensureStash(state.topLevel())) {
        case GitClient::StashUnchanged:
        case GitClient::Stashed:
        case GitClient::NotStashed:
            m_gitClient->synchronousPull(state.topLevel());
        default:
        break;
    }
}

void GitPlugin::push()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasTopLevel(), return)
    m_gitClient->synchronousPush(state.topLevel());
}

// Retrieve member function of git client stored as user data of action
static inline GitClientMemberFunc memberFunctionFromAction(const QObject *o)
{
    if (o) {
        if (const QAction *action = qobject_cast<const QAction *>(o)) {
            const QVariant v = action->data();
            if (qVariantCanConvert<GitClientMemberFunc>(v))
                return qVariantValue<GitClientMemberFunc>(v);
        }
    }
    return 0;
}

void GitPlugin::gitClientMemberFuncRepositoryAction()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasTopLevel(), return)
    // Retrieve member function and invoke on repository
    GitClientMemberFunc func = memberFunctionFromAction(sender());
    QTC_ASSERT(func, return);
    (m_gitClient->*func)(state.topLevel());
}

void GitPlugin::cleanProject()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasProject(), return)
    cleanRepository(state.currentProjectPath());
}

void GitPlugin::cleanRepository()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasTopLevel(), return);
    cleanRepository(state.topLevel());
}

void GitPlugin::cleanRepository(const QString &directory)
{
    // Find files to be deleted
    QString errorMessage;
    QStringList files;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool gotFiles = m_gitClient->synchronousCleanList(directory, &files, &errorMessage);
    QApplication::restoreOverrideCursor();

    QWidget *parent = Core::ICore::mainWindow();
    if (!gotFiles) {
        QMessageBox::warning(parent, tr("Unable to retrieve file list"),
                             errorMessage);
        return;
    }
    if (files.isEmpty()) {
        QMessageBox::information(parent, tr("Repository Clean"),
                                 tr("The repository is clean."));
        return;
    }
    // Clean the trailing slash of directories
    const QChar slash = QLatin1Char('/');
    const QStringList::iterator end = files.end();
    for (QStringList::iterator it = files.begin(); it != end; ++it)
        if (it->endsWith(slash))
            it->truncate(it->size() - 1);

    // Show in dialog
    VcsBase::CleanDialog dialog(parent);
    dialog.setFileList(directory, files);
    dialog.exec();
}

// If the file is modified in an editor, make sure it is saved.
static bool ensureFileSaved(const QString &fileName)
{
    const QList<Core::IEditor*> editors = Core::EditorManager::instance()->editorsForFileName(fileName);
    if (editors.isEmpty())
        return true;
    Core::IDocument *document = editors.front()->document();
    if (!document || !document->isModified())
        return true;
    bool canceled;
    QList<Core::IDocument *> documents;
    documents << document;
    Core::DocumentManager::saveModifiedDocuments(documents, &canceled);
    return !canceled;
}

void GitPlugin::applyCurrentFilePatch()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasPatchFile() && state.hasTopLevel(), return);
    const QString patchFile = state.currentPatchFile();
    if (!ensureFileSaved(patchFile))
        return;
    applyPatch(state.topLevel(), patchFile);
}

void GitPlugin::promptApplyPatch()
{
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasTopLevel(), return);
    applyPatch(state.topLevel(), QString());
}

void GitPlugin::applyPatch(const QString &workingDirectory, QString file)
{
    // Ensure user has been notified about pending changes
    switch (m_gitClient->ensureStash(workingDirectory)) {
    case GitClient::StashUnchanged:
    case GitClient::Stashed:
    case GitClient::NotStashed:
        break;
    default:
        return;
    }
    // Prompt for file
    if (file.isEmpty()) {
        const QString filter = tr("Patches (*.patch *.diff)");
        file =  QFileDialog::getOpenFileName(Core::ICore::mainWindow(),
                                             tr("Choose Patch"),
                                             QString(), filter);
        if (file.isEmpty())
            return;
    }
    // Run!
    VcsBase::VcsBaseOutputWindow *outwin = VcsBase::VcsBaseOutputWindow::instance();
    QString errorMessage;
    if (m_gitClient->synchronousApplyPatch(workingDirectory, file, &errorMessage)) {
        if (errorMessage.isEmpty()) {
            outwin->append(tr("Patch %1 successfully applied to %2").arg(file, workingDirectory));
        } else {
            outwin->append(errorMessage);
        }
    } else {
        outwin->appendError(errorMessage);
    }
}

void GitPlugin::stash()
{
    // Simple stash without prompt, reset repo.
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasTopLevel(), return)
    const QString id = m_gitClient->synchronousStash(state.topLevel(), QString(), 0);
    if (!id.isEmpty() && m_stashDialog)
        m_stashDialog->refresh(state.topLevel(), true);
}

void GitPlugin::stashSnapshot()
{
    // Prompt for description, restore immediately and keep on working.
    const VcsBase::VcsBasePluginState state = currentState();
    QTC_ASSERT(state.hasTopLevel(), return)
    const QString id = m_gitClient->synchronousStash(state.topLevel(), QString(), GitClient::StashImmediateRestore|GitClient::StashPromptDescription);
    if (!id.isEmpty() && m_stashDialog)
        m_stashDialog->refresh(state.topLevel(), true);
}

// Create a non-modal dialog with refresh method or raise if it exists
template <class NonModalDialog>
    inline void showNonModalDialog(const QString &topLevel,
                                   QPointer<NonModalDialog> &dialog)
{
    if (dialog) {
        dialog->show();
        dialog->raise();
    } else {
        dialog = new NonModalDialog(Core::ICore::mainWindow());
        dialog->refresh(topLevel, true);
        dialog->show();
    }
}

void GitPlugin::branchList()
{
   showNonModalDialog(currentState().topLevel(), m_branchDialog);
}

void GitPlugin::remoteList()
{
    showNonModalDialog(currentState().topLevel(), m_remoteDialog);
}

void GitPlugin::stashList()
{
    showNonModalDialog(currentState().topLevel(), m_stashDialog);
}

void GitPlugin::updateActions(VcsBase::VcsBasePlugin::ActionState as)
{
    const bool repositoryEnabled = currentState().hasTopLevel();
    if (m_stashDialog)
        m_stashDialog->refresh(currentState().topLevel(), false);
    if (m_branchDialog)
        m_branchDialog->refresh(currentState().topLevel(), false);
    if (m_remoteDialog)
        m_remoteDialog->refresh(currentState().topLevel(), false);

    m_commandLocator->setEnabled(repositoryEnabled);
    if (!enableMenuAction(as, m_menuAction))
        return;
    // Note: This menu is visible if there is no repository. Only
    // 'Create Repository'/'Show' actions should be available.
    const QString fileName = currentState().currentFileName();
    foreach (Utils::ParameterAction *fileAction, m_fileActions)
        fileAction->setParameter(fileName);
    // If the current file looks like a patch, offer to apply
    m_applyCurrentFilePatchAction->setParameter(currentState().currentPatchFileDisplayName());

    const QString projectName = currentState().currentProjectName();
    foreach (Utils::ParameterAction *projectAction, m_projectActions)
        projectAction->setParameter(projectName);

    foreach (QAction *repositoryAction, m_repositoryActions)
        repositoryAction->setEnabled(repositoryEnabled);

    // Prompts for repo.
    m_showAction->setEnabled(true);
}

void GitPlugin::showCommit()
{
    const VcsBase::VcsBasePluginState state = currentState();

    if (!m_changeSelectionDialog)
        m_changeSelectionDialog = new ChangeSelectionDialog();

    if (state.hasFile())
        m_changeSelectionDialog->setWorkingDirectory(state.currentFileDirectory());
    else if (state.hasTopLevel())
        m_changeSelectionDialog->setWorkingDirectory(state.topLevel());

    if (m_changeSelectionDialog->exec() != QDialog::Accepted)
        return;
    const QString change = m_changeSelectionDialog->change();
    if (change.isEmpty())
        return;

    m_gitClient->show(m_changeSelectionDialog->workingDirectory(), change);
}

const GitSettings &GitPlugin::settings() const
{
    return m_settings;
}

void GitPlugin::setSettings(const GitSettings &s)
{
    if (s == m_settings)
        return;

    m_settings = s;
    m_gitClient->saveSettings();
    static_cast<GitVersionControl *>(versionControl())->emitConfigurationChanged();
}

GitClient *GitPlugin::gitClient() const
{
    return m_gitClient;
}

Q_EXPORT_PLUGIN(GitPlugin)
