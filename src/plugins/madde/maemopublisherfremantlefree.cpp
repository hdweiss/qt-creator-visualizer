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
#include "maemopublisherfremantlefree.h"

#include "maemoglobal.h"
#include "maemopackagecreationstep.h"
#include "maemopublishingfileselectiondialog.h"
#include "qt4maemodeployconfiguration.h"
#include "qt4maemotarget.h"

#include <coreplugin/idocument.h>
#include <projectexplorer/project.h>
#include <projectexplorer/target.h>
#include <qt4projectmanager/qmakestep.h>
#include <qt4projectmanager/qt4buildconfiguration.h>
#include <qtsupport/baseqtversion.h>
#include <remotelinux/deployablefilesperprofile.h>
#include <remotelinux/deploymentinfo.h>
#include <utils/fileutils.h>
#include <utils/qtcassert.h>
#include <utils/ssh/sshremoteprocessrunner.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QIcon>

using namespace Core;
using namespace Qt4ProjectManager;
using namespace RemoteLinux;
using namespace Utils;

namespace Madde {
namespace Internal {

MaemoPublisherFremantleFree::MaemoPublisherFremantleFree(const ProjectExplorer::Project *project,
    QObject *parent) :
    QObject(parent),
    m_project(project),
    m_state(Inactive),
    m_sshParams(SshConnectionParameters::DefaultProxy),
    m_uploader(0)
{
    m_sshParams.authenticationType = SshConnectionParameters::AuthenticationByKey;
    m_sshParams.timeout = 30;
    m_sshParams.port = 22;
    m_process = new QProcess(this);
    connect(m_process, SIGNAL(finished(int,QProcess::ExitStatus)),
        SLOT(handleProcessFinished()));
    connect(m_process, SIGNAL(error(QProcess::ProcessError)),
        SLOT(handleProcessError(QProcess::ProcessError)));
    connect(m_process, SIGNAL(readyReadStandardOutput()),
        SLOT(handleProcessStdOut()));
    connect(m_process, SIGNAL(readyReadStandardError()),
        SLOT(handleProcessStdErr()));
}

MaemoPublisherFremantleFree::~MaemoPublisherFremantleFree()
{
    QTC_ASSERT(m_state == Inactive, return);
    m_process->kill();
}

void MaemoPublisherFremantleFree::publish()
{
    createPackage();
}

void MaemoPublisherFremantleFree::setSshParams(const QString &hostName,
    const QString &userName, const QString &keyFile, const QString &remoteDir)
{
    Q_ASSERT(m_doUpload);
    m_sshParams.host = hostName;
    m_sshParams.userName = userName;
    m_sshParams.privateKeyFile = keyFile;
    m_remoteDir = remoteDir;
}

void MaemoPublisherFremantleFree::cancel()
{
    finishWithFailure(tr("Canceled."), tr("Publishing canceled by user."));
}

void MaemoPublisherFremantleFree::createPackage()
{
    setState(CopyingProjectDir);

    const QStringList &problems = findProblems();
    if (!problems.isEmpty()) {
        const QLatin1String separator("\n- ");
        finishWithFailure(tr("The project is missing some information "
            "important to publishing:") + separator + problems.join(separator),
            tr("Publishing failed: Missing project information."));
        return;
    }

    m_tmpProjectDir = tmpDirContainer() + QLatin1Char('/')
        + m_project->displayName();
    if (QFileInfo(tmpDirContainer()).exists()) {
        emit progressReport(tr("Removing left-over temporary directory ..."));
        QString error;
        if (!Utils::FileUtils::removeRecursively(tmpDirContainer(), &error)) {
            finishWithFailure(tr("Error removing temporary directory: %1").arg(error),
                tr("Publishing failed: Could not create source package."));
            return;
        }
    }

    emit progressReport(tr("Setting up temporary directory ..."));
    if (!QDir::temp().mkdir(QFileInfo(tmpDirContainer()).fileName())) {
        finishWithFailure(tr("Error: Could not create temporary directory."),
            tr("Publishing failed: Could not create source package."));
        return;
    }
    if (!copyRecursively(m_project->projectDirectory(), m_tmpProjectDir)) {
        finishWithFailure(tr("Error: Could not copy project directory."),
            tr("Publishing failed: Could not create source package."));
        return;
    }
    if (!fixNewlines()) {
        finishWithFailure(tr("Error: Could not fix newlines."),
            tr("Publishing failed: Could not create source package."));
        return;
    }

    QString error;
    if (!updateDesktopFiles(&error)) {
        finishWithFailure(error,
            tr("Publishing failed: Could not create package."));
        return;
    }

    emit progressReport(tr("Cleaning up temporary directory ..."));
    AbstractMaemoPackageCreationStep::preparePackagingProcess(m_process,
            m_buildConfig, m_tmpProjectDir);
    setState(RunningQmake);
    ProjectExplorer::AbstractProcessStep * const qmakeStep
        = m_buildConfig->qmakeStep();
    qmakeStep->init();
    const ProjectExplorer::ProcessParameters * const pp
        = qmakeStep->processParameters();
    m_process->start(pp->effectiveCommand() + QLatin1String(" ")
        + pp->effectiveArguments());
}

bool MaemoPublisherFremantleFree::copyRecursively(const QString &srcFilePath,
    const QString &tgtFilePath)
{
    if (m_state == Inactive)
        return true;

    QFileInfo srcFileInfo(srcFilePath);
    if (srcFileInfo.isDir()) {
        if (srcFileInfo == QFileInfo(m_project->projectDirectory()
               + QLatin1String("/debian")))
            return true;
        QString actualSourcePath = srcFilePath;
        QString actualTargetPath = tgtFilePath;

        if (srcFileInfo.fileName() == QLatin1String("qtc_packaging")) {
            actualSourcePath += QLatin1String("/debian_fremantle");
            actualTargetPath.replace(QRegExp(QLatin1String("qtc_packaging$")),
                QLatin1String("debian"));
        }

        QDir targetDir(actualTargetPath);
        targetDir.cdUp();
        if (!targetDir.mkdir(QFileInfo(actualTargetPath).fileName())) {
            emit progressReport(tr("Failed to create directory '%1'.")
                .arg(QDir::toNativeSeparators(actualTargetPath)), ErrorOutput);
            return false;
        }
        QDir sourceDir(actualSourcePath);
        QStringList fileNames = sourceDir.entryList(QDir::Files | QDir::Hidden
            | QDir::System | QDir::Dirs | QDir::NoDotAndDotDot);
        foreach (const QString &fileName, fileNames) {
            if (!copyRecursively(actualSourcePath + QLatin1Char('/') + fileName,
                    actualTargetPath + QLatin1Char('/') + fileName))
                return false;
        }
    } else {
        if (tgtFilePath == m_tmpProjectDir + QLatin1String("/debian/rules")) {
            Utils::FileReader reader;
            if (!reader.fetch(srcFilePath)) {
                emit progressReport(reader.errorString(), ErrorOutput);
                return false;
            }
            QByteArray rulesContents = reader.data();
            rulesContents.replace("$(MAKE) clean", "# $(MAKE) clean");
            rulesContents.replace("# Add here commands to configure the package.",
                "qmake " + QFileInfo(m_project->document()->fileName()).fileName().toLocal8Bit());
            MaemoDebianPackageCreationStep::ensureShlibdeps(rulesContents);
            Utils::FileSaver saver(tgtFilePath);
            saver.write(rulesContents);
            if (!saver.finalize()) {
                emit progressReport(saver.errorString(), ErrorOutput);
                return false;
            }
            QFile rulesFile(tgtFilePath);
            if (!rulesFile.setPermissions(rulesFile.permissions() | QFile::ExeUser)) {
                emit progressReport(tr("Could not set execute permissions for rules file: %1")
                    .arg(rulesFile.errorString()));
                return false;
            }
        } else {
            QFile srcFile(srcFilePath);
            if (!srcFile.copy(tgtFilePath)) {
                emit progressReport(tr("Could not copy file '%1' to '%2': %3.")
                    .arg(QDir::toNativeSeparators(srcFilePath),
                         QDir::toNativeSeparators(tgtFilePath),
                         srcFile.errorString()));
                return false;
            }
        }
    }
    return true;
}

bool MaemoPublisherFremantleFree::fixNewlines()
{
    QDir debianDir(m_tmpProjectDir + QLatin1String("/debian"));
    const QStringList &fileNames = debianDir.entryList(QDir::Files);
    foreach (const QString &fileName, fileNames) {
        QString filePath = debianDir.filePath(fileName);
        Utils::FileReader reader;
        if (!reader.fetch(filePath))
            return false;
        QByteArray contents = reader.data();
        const QByteArray crlf("\r\n");
        if (!contents.contains(crlf))
            continue;
        contents.replace(crlf, "\n");
        Utils::FileSaver saver(filePath);
        saver.write(contents);
        if (!saver.finalize())
            return false;
    }
    return true;
}

void MaemoPublisherFremantleFree::handleProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart)
        handleProcessFinished(true);
}

void MaemoPublisherFremantleFree::handleProcessFinished()
{
    handleProcessFinished(false);
}

void MaemoPublisherFremantleFree::handleProcessStdOut()
{
    if (m_state == RunningQmake || m_state == RunningMakeDistclean
            || m_state == BuildingPackage) {
        emit progressReport(QString::fromLocal8Bit(m_process->readAllStandardOutput()),
            ToolStatusOutput);
    }
}

void MaemoPublisherFremantleFree::handleProcessStdErr()
{
    if (m_state == RunningQmake || m_state == RunningMakeDistclean
            || m_state == BuildingPackage) {
        emit progressReport(QString::fromLocal8Bit(m_process->readAllStandardError()),
            ToolErrorOutput);
    }
}

void MaemoPublisherFremantleFree::handleProcessFinished(bool failedToStart)
{
    QTC_ASSERT(m_state == RunningQmake || m_state == RunningMakeDistclean
        || m_state == BuildingPackage || m_state == Inactive, return);

    switch (m_state) {
    case RunningQmake:
        if (failedToStart || m_process->exitStatus() != QProcess::NormalExit
                ||m_process->exitCode() != 0) {
            runDpkgBuildPackage();
        } else {
            setState(RunningMakeDistclean);
            m_process->start(m_buildConfig->makeCommand(),
                QStringList() << QLatin1String("distclean"));
        }
        break;
    case RunningMakeDistclean:
        runDpkgBuildPackage();
        break;
    case BuildingPackage: {
        QString error;
        if (failedToStart) {
            error = tr("Error: Failed to start dpkg-buildpackage.");
        } else if (m_process->exitStatus() != QProcess::NormalExit
                   || m_process->exitCode() != 0) {
            error = tr("Error: dpkg-buildpackage did not succeed.");
        }

        if (!error.isEmpty()) {
            finishWithFailure(error, tr("Package creation failed."));
            return;
        }

        QDir dir(tmpDirContainer());
        const QStringList &fileNames = dir.entryList(QDir::Files);
        foreach (const QString &fileName, fileNames) {
            const QString filePath
                = tmpDirContainer() + QLatin1Char('/') + fileName;
            if (fileName.endsWith(QLatin1String(".dsc")))
                m_filesToUpload.append(filePath);
            else
                m_filesToUpload.prepend(filePath);
        }
        if (!m_doUpload) {
            emit progressReport(tr("Done."));
            QStringList nativeFilePaths;
            foreach (const QString &filePath, m_filesToUpload)
                nativeFilePaths << QDir::toNativeSeparators(filePath);
            m_resultString = tr("Packaging finished successfully. "
                "The following files were created:\n")
                + nativeFilePaths.join(QLatin1String("\n"));
            setState(Inactive);
        } else {
            uploadPackage();
        }
        break;
    }
    default:
        break;
    }
}

void MaemoPublisherFremantleFree::runDpkgBuildPackage()
{
    MaemoPublishingFileSelectionDialog d(m_tmpProjectDir);
    if (d.exec() == QDialog::Rejected) {
        cancel();
        return;
    }
    foreach (const QString &filePath, d.filesToExclude()) {
        QString error;
        if (!Utils::FileUtils::removeRecursively(filePath, &error)) {
            finishWithFailure(error,
                tr("Publishing failed: Could not create package."));
        }
    }

    QtSupport::BaseQtVersion *lqt = m_buildConfig->qtVersion();
    if (!lqt)
        finishWithFailure(QString(), tr("No Qt version set."));

    if (m_state == Inactive)
        return;
    setState(BuildingPackage);
    emit progressReport(tr("Building source package..."));
    const QStringList args = QStringList() << QLatin1String("dpkg-buildpackage")
        << QLatin1String("-S") << QLatin1String("-us") << QLatin1String("-uc");
    MaemoGlobal::callMad(*m_process, args, lqt->qmakeCommand().toString(), true);
}

// We have to implement the SCP protocol, because the maemo.org
// webmaster refuses to enable SFTP "for security reasons" ...
void MaemoPublisherFremantleFree::uploadPackage()
{
    if (!m_uploader)
        m_uploader = new SshRemoteProcessRunner(this);
    connect(m_uploader, SIGNAL(processStarted()), SLOT(handleScpStarted()));
    connect(m_uploader, SIGNAL(connectionError()), SLOT(handleConnectionError()));
    connect(m_uploader, SIGNAL(processClosed(int)), SLOT(handleUploadJobFinished(int)));
    connect(m_uploader, SIGNAL(processOutputAvailable(QByteArray)),
        SLOT(handleScpStdOut(QByteArray)));
    emit progressReport(tr("Starting scp ..."));
    setState(StartingScp);
    m_uploader->run("scp -td " + m_remoteDir.toUtf8(), m_sshParams);
}

void MaemoPublisherFremantleFree::handleScpStarted()
{
    QTC_ASSERT(m_state == StartingScp || m_state == Inactive, return);

    if (m_state == StartingScp)
        prepareToSendFile();
}

void MaemoPublisherFremantleFree::handleConnectionError()
{
    if (m_state != Inactive) {
        finishWithFailure(tr("SSH error: %1").arg(m_uploader->lastConnectionErrorString()),
            tr("Upload failed."));
    }
}

void MaemoPublisherFremantleFree::handleUploadJobFinished(int exitStatus)
{
    QTC_ASSERT(m_state == PreparingToUploadFile || m_state == UploadingFile || m_state ==Inactive,
        return);

    if (m_state != Inactive && (exitStatus != SshRemoteProcess::ExitedNormally
            || m_uploader->processExitCode() != 0)) {
        QString error;
        if (exitStatus != SshRemoteProcess::ExitedNormally) {
            error = tr("Error uploading file: %1.")
                .arg(m_uploader->processErrorString());
        } else {
            error = tr("Error uploading file.");
        }
        finishWithFailure(error, tr("Upload failed."));
    }
}

void MaemoPublisherFremantleFree::prepareToSendFile()
{
    if (m_filesToUpload.isEmpty()) {
        emit progressReport(tr("All files uploaded."));
        m_resultString = tr("Upload succeeded. You should shortly "
            "receive an email informing you about the outcome "
            "of the build process.");
        setState(Inactive);
        return;
    }

    setState(PreparingToUploadFile);
    const QString &nextFilePath = m_filesToUpload.first();
    emit progressReport(tr("Uploading file %1 ...")
        .arg(QDir::toNativeSeparators(nextFilePath)));
    QFileInfo info(nextFilePath);
    m_uploader->writeDataToProcess("C0644 " + QByteArray::number(info.size())
        + ' ' + info.fileName().toUtf8() + '\n');
}

void MaemoPublisherFremantleFree::sendFile()
{
    Q_ASSERT(!m_filesToUpload.isEmpty());
    Q_ASSERT(m_state == PreparingToUploadFile);

    setState(UploadingFile);
    const QString filePath = m_filesToUpload.takeFirst();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        finishWithFailure(tr("Cannot open file for reading: %1.")
            .arg(file.errorString()), tr("Upload failed."));
        return;
    }
    qint64 bytesToSend = file.size();
    while (bytesToSend > 0) {
        const QByteArray &data
            = file.read(qMin(bytesToSend, Q_INT64_C(1024*1024)));
        if (data.count() == 0) {
            finishWithFailure(tr("Cannot read file: %1").arg(file.errorString()),
                tr("Upload failed."));
            return;
        }
        m_uploader->writeDataToProcess(data);
        bytesToSend -= data.size();
        QCoreApplication::processEvents();
        if (m_state == Inactive)
            return;
    }
    m_uploader->writeDataToProcess(QByteArray(1, '\0'));
}

void MaemoPublisherFremantleFree::handleScpStdOut(const QByteArray &output)
{
    QTC_ASSERT(m_state == PreparingToUploadFile || m_state == UploadingFile || m_state == Inactive,
        return);

    if (m_state == Inactive)
        return;

    m_scpOutput += output;
    if (m_scpOutput == QByteArray(1, '\0')) {
        m_scpOutput.clear();
        switch (m_state) {
        case PreparingToUploadFile:
            sendFile();
            break;
        case UploadingFile:
            prepareToSendFile();
            break;
        default:
            break;
        }
    } else if (m_scpOutput.endsWith('\n')) {
        const QByteArray error = m_scpOutput.mid(1, m_scpOutput.count() - 2);
        QString progressError;
        if (!error.isEmpty()) {
            progressError = tr("Error uploading file: %1.")
                .arg(QString::fromUtf8(error));
        } else {
            progressError = tr("Error uploading file.");
        }
        finishWithFailure(progressError, tr("Upload failed."));
    }
}

QString MaemoPublisherFremantleFree::tmpDirContainer() const
{
    return QDir::tempPath() + QLatin1String("/qtc_packaging_")
        + m_project->displayName();
}

void MaemoPublisherFremantleFree::finishWithFailure(const QString &progressMsg,
    const QString &resultMsg)
{
    if (!progressMsg.isEmpty())
        emit progressReport(progressMsg, ErrorOutput);
    m_resultString = resultMsg;
    setState(Inactive);
}

bool MaemoPublisherFremantleFree::updateDesktopFiles(QString *error) const
{
    bool success = true;
    const Qt4MaemoDeployConfiguration * const deployConfig
        = qobject_cast<Qt4MaemoDeployConfiguration *>(m_buildConfig->target()->activeDeployConfiguration());
    const DeploymentInfo * const deploymentInfo = deployConfig->deploymentInfo();
    for (int i = 0; i < deploymentInfo->modelCount(); ++i) {
        const DeployableFilesPerProFile * const model = deploymentInfo->modelAt(i);
        QString desktopFilePath = deployConfig->localDesktopFilePath(model);
        if (desktopFilePath.isEmpty())
            continue;
        desktopFilePath.replace(model->projectDir(), m_tmpProjectDir);
        const QString executableFilePath = model->remoteExecutableFilePath();
        if (executableFilePath.isEmpty()) {
            qDebug("%s: Skipping subproject %s with missing deployment information.",
                Q_FUNC_INFO, qPrintable(model->proFilePath()));
            continue;
        }
        Utils::FileReader reader;
        if (!reader.fetch(desktopFilePath, error)) {
            success = false;
            continue;
        }
        QByteArray desktopFileContents = reader.data();
        bool fileNeedsUpdate = addOrReplaceDesktopFileValue(desktopFileContents,
            "Exec", executableFilePath.toUtf8());
        if (fileNeedsUpdate) {
            Utils::FileSaver saver(desktopFilePath);
            saver.write(desktopFileContents);
            if (!saver.finalize(error))
                success = false;
        }
    }
    return success;
}

bool MaemoPublisherFremantleFree::addOrReplaceDesktopFileValue(QByteArray &fileContent,
    const QByteArray &key, const QByteArray &newValue) const
{
    const int keyPos = fileContent.indexOf(key + '=');
    if (keyPos == -1) {
        if (!fileContent.endsWith('\n'))
            fileContent += '\n';
        fileContent += key + '=' + newValue + '\n';
        return true;
    }
    int nextNewlinePos = fileContent.indexOf('\n', keyPos);
    if (nextNewlinePos == -1)
        nextNewlinePos = fileContent.count();
    const int replacePos = keyPos + key.count() + 1;
    const int replaceCount = nextNewlinePos - replacePos;
    const QByteArray &oldValue = fileContent.mid(replacePos, replaceCount);
    if (oldValue == newValue)
        return false;
    fileContent.replace(replacePos, replaceCount, newValue);
    return true;
}

QStringList MaemoPublisherFremantleFree::findProblems() const
{
    QStringList problems;
    const Qt4Maemo5Target * const target
        = qobject_cast<Qt4Maemo5Target *>(m_buildConfig->target());
    const QString &description = target->shortDescription();
    if (description.trimmed().isEmpty()) {
        problems << tr("The package description is empty. You must set one "
            "in Projects -> Run -> Create Package -> Details.");
    } else if (description.contains(QLatin1String("insert up to"))) {
        problems << tr("The package description is '%1', which is probably "
            "not what you want. Please change it in "
            "Projects -> Run -> Create Package -> Details.").arg(description);
    }
    QString dummy;
    if (target->packageManagerIcon(&dummy).isNull())
        problems << tr("You have not set an icon for the package manager. "
            "The icon must be set in Projects -> Run -> Create Package -> Details.");
    return problems;
}

void MaemoPublisherFremantleFree::setState(State newState)
{
    if (m_state == newState)
        return;
    const State oldState = m_state;
    m_state = newState;
    if (m_state == Inactive) {
        switch (oldState) {
        case RunningQmake:
        case RunningMakeDistclean:
        case BuildingPackage:
            disconnect(m_process, 0, this, 0);
            m_process->terminate();
            break;
        case StartingScp:
        case PreparingToUploadFile:
        case UploadingFile:
            // TODO: Can we ensure the remote scp exits, e.g. by sending
            //       an illegal sequence of bytes? (Probably not, if
            //       we are currently uploading a file.)
            disconnect(m_uploader, 0, this, 0);
            break;
        default:
            break;
        }
        emit finished();
    }
}

} // namespace Internal
} // namespace Madde
