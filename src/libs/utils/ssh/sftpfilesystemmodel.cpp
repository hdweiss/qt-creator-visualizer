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
#include "sftpfilesystemmodel.h"

#include "sftpchannel.h"
#include "sshconnection.h"
#include "sshconnectionmanager.h"

#include <utils/qtcassert.h>

#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QString>

namespace Utils {
namespace Internal {
namespace {

class SftpDirNode;
class SftpFileNode
{
public:
    SftpFileNode() : parent(0) { }
    virtual ~SftpFileNode() { }

    QString path;
    SftpFileInfo fileInfo;
    SftpDirNode *parent;
};

class SftpDirNode : public SftpFileNode
{
public:
    SftpDirNode() : lsState(LsNotYetCalled) { }
    ~SftpDirNode() { qDeleteAll(children); }

    enum { LsNotYetCalled, LsRunning, LsFinished } lsState;
    QList<SftpFileNode *> children;
};

typedef QHash<SftpJobId, SftpDirNode *> DirNodeHash;

SftpFileNode *indexToFileNode(const QModelIndex &index)
{
    return static_cast<SftpFileNode *>(index.internalPointer());
}

SftpDirNode *indexToDirNode(const QModelIndex &index)
{
    SftpFileNode * const fileNode = indexToFileNode(index);
    QTC_CHECK(fileNode);
    return dynamic_cast<SftpDirNode *>(fileNode);
}

} // anonymous namespace

class SftpFileSystemModelPrivate
{
public:
    SshConnection::Ptr sshConnection;
    SftpChannel::Ptr sftpChannel;
    QString rootDirectory;
    SftpFileNode *rootNode;
    SftpJobId statJobId;
    DirNodeHash lsOps;
    QList<SftpJobId> externalJobs;
};
} // namespace Internal

using namespace Internal;

SftpFileSystemModel::SftpFileSystemModel(QObject *parent)
    : QAbstractItemModel(parent), d(new SftpFileSystemModelPrivate)
{
    d->rootDirectory = QLatin1String("/");
    d->rootNode = 0;
    d->statJobId = SftpInvalidJob;
}

SftpFileSystemModel::~SftpFileSystemModel()
{
    shutDown();
    delete d;
}

void SftpFileSystemModel::setSshConnection(const SshConnectionParameters &sshParams)
{
    QTC_ASSERT(!d->sshConnection, return);
    d->sshConnection = SshConnectionManager::instance().acquireConnection(sshParams);
    connect(d->sshConnection.data(), SIGNAL(error(Utils::SshError)),
        SLOT(handleSshConnectionFailure()));
    if (d->sshConnection->state() == SshConnection::Connected)
        handleSshConnectionEstablished();
    connect(d->sshConnection.data(), SIGNAL(connected()), SLOT(handleSshConnectionEstablished()));
    if (d->sshConnection->state() == SshConnection::Unconnected)
        d->sshConnection->connectToHost();
}

void SftpFileSystemModel::setRootDirectory(const QString &path)
{
    beginResetModel();
    d->rootDirectory = path;
    delete d->rootNode;
    d->rootNode = 0;
    d->lsOps.clear();
    d->statJobId = SftpInvalidJob;
    endResetModel();
    statRootDirectory();
}

QString SftpFileSystemModel::rootDirectory() const
{
    return d->rootDirectory;
}

SftpJobId SftpFileSystemModel::downloadFile(const QModelIndex &index, const QString &targetFilePath)
{
    QTC_ASSERT(d->rootNode, return SftpInvalidJob);
    const SftpFileNode * const fileNode = indexToFileNode(index);
    QTC_ASSERT(fileNode, return SftpInvalidJob);
    QTC_ASSERT(fileNode->fileInfo.type == FileTypeRegular, return SftpInvalidJob);
    const SftpJobId jobId = d->sftpChannel->downloadFile(fileNode->path, targetFilePath,
        SftpOverwriteExisting);
    if (jobId != SftpInvalidJob)
        d->externalJobs << jobId;
    return jobId;
}

int SftpFileSystemModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 2; // type + name
}

QVariant SftpFileSystemModel::data(const QModelIndex &index, int role) const
{
    const SftpFileNode * const node = indexToFileNode(index);
    if (index.column() == 0 && role == Qt::DecorationRole) {
        switch (node->fileInfo.type) {
        case FileTypeRegular:
        case FileTypeOther:
            return QIcon(QLatin1String(":/core/images/unknownfile.png"));
        case FileTypeDirectory:
            return QIcon(QLatin1String(":/core/images/dir.png"));
        case FileTypeUnknown:
            return QIcon(QLatin1String(":/core/images/help.png")); // Shows a question mark.
        }
    }
    if (index.column() == 1) {
        if (role == Qt::DisplayRole)
            return node->fileInfo.name;
        if (role == PathRole)
            return node->path;
    }
    return QVariant();
}

Qt::ItemFlags SftpFileSystemModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

QVariant SftpFileSystemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return QVariant();
    if (role != Qt::DisplayRole)
        return QVariant();
    if (section == 0)
        return tr("File Type");
    if (section == 1)
        return tr("File Name");
    return QVariant();
}

QModelIndex SftpFileSystemModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || row >= rowCount(parent) || column < 0 || column >= columnCount(parent))
        return QModelIndex();
    if (!d->rootNode)
        return QModelIndex();
    if (!parent.isValid())
        return createIndex(row, column, d->rootNode);
    const SftpDirNode * const parentNode = indexToDirNode(parent);
    QTC_ASSERT(parentNode, return QModelIndex());
    QTC_ASSERT(row < parentNode->children.count(), return QModelIndex());
    SftpFileNode * const childNode = parentNode->children.at(row);
    return createIndex(row, column, childNode);
}

QModelIndex SftpFileSystemModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) // Don't assert on this, since the model tester tries it.
        return QModelIndex();

    const SftpFileNode * const childNode = indexToFileNode(child);
    QTC_ASSERT(childNode, return QModelIndex());
    if (childNode == d->rootNode)
        return QModelIndex();
    SftpDirNode * const parentNode = childNode->parent;
    if (parentNode == d->rootNode)
        return createIndex(0, 0, d->rootNode);
    const SftpDirNode * const grandParentNode = parentNode->parent;
    QTC_ASSERT(grandParentNode, return QModelIndex());
    return createIndex(grandParentNode->children.indexOf(parentNode), 0, parentNode);
}

int SftpFileSystemModel::rowCount(const QModelIndex &parent) const
{
    if (!d->rootNode)
        return 0;
    if (!parent.isValid())
        return 1;
    if (parent.column() != 0)
        return 0;
    SftpDirNode * const dirNode = indexToDirNode(parent);
    if (!dirNode)
        return 0;
    if (dirNode->lsState != SftpDirNode::LsNotYetCalled)
        return dirNode->children.count();
    d->lsOps.insert(d->sftpChannel->listDirectory(dirNode->path), dirNode);
    dirNode->lsState = SftpDirNode::LsRunning;
    return 0;
}

void SftpFileSystemModel::statRootDirectory()
{
    d->statJobId = d->sftpChannel->statFile(d->rootDirectory);
}

void SftpFileSystemModel::shutDown()
{
    if (d->sftpChannel) {
        disconnect(d->sftpChannel.data(), 0, this, 0);
        d->sftpChannel->closeChannel();
        d->sftpChannel.clear();
    }
    if (d->sshConnection) {
        disconnect(d->sshConnection.data(), 0, this, 0);
        SshConnectionManager::instance().releaseConnection(d->sshConnection);
        d->sshConnection.clear();
    }
    delete d->rootNode;
    d->rootNode = 0;
}

void SftpFileSystemModel::handleSshConnectionFailure()
{
    emit connectionError(d->sshConnection->errorString());
    beginResetModel();
    shutDown();
    endResetModel();
}

void SftpFileSystemModel::handleSftpChannelInitialized()
{
    connect(d->sftpChannel.data(),
        SIGNAL(fileInfoAvailable(Utils::SftpJobId, QList<Utils::SftpFileInfo>)),
        SLOT(handleFileInfo(Utils::SftpJobId, QList<Utils::SftpFileInfo>)));
    connect(d->sftpChannel.data(), SIGNAL(finished(Utils::SftpJobId, QString)),
        SLOT(handleSftpJobFinished(Utils::SftpJobId, QString)));
    statRootDirectory();
}

void SftpFileSystemModel::handleSshConnectionEstablished()
{
    d->sftpChannel = d->sshConnection->createSftpChannel();
    connect(d->sftpChannel.data(), SIGNAL(initialized()), SLOT(handleSftpChannelInitialized()));
    connect(d->sftpChannel.data(), SIGNAL(initializationFailed(QString)),
        SLOT(handleSftpChannelInitializationFailed(QString)));
    d->sftpChannel->initialize();
}

void SftpFileSystemModel::handleSftpChannelInitializationFailed(const QString &reason)
{
    emit connectionError(reason);
    beginResetModel();
    shutDown();
    endResetModel();
}

void SftpFileSystemModel::handleFileInfo(SftpJobId jobId, const QList<SftpFileInfo> &fileInfoList)
{
    if (jobId == d->statJobId) {
        QTC_ASSERT(!d->rootNode, return);
        beginInsertRows(QModelIndex(), 0, 0);
        d->rootNode = new SftpDirNode;
        d->rootNode->path = d->rootDirectory;
        d->rootNode->fileInfo = fileInfoList.first();
        d->rootNode->fileInfo.name = d->rootDirectory == QLatin1String("/")
            ? d->rootDirectory : QFileInfo(d->rootDirectory).fileName();
        endInsertRows();
        return;
    }
    SftpDirNode * const parentNode = d->lsOps.value(jobId);
    QTC_ASSERT(parentNode, return);
    QList<SftpFileInfo> filteredList;
    foreach (const SftpFileInfo &fi, fileInfoList) {
        if (fi.name != QLatin1String(".") && fi.name != QLatin1String(".."))
            filteredList << fi;
    }
    if (filteredList.isEmpty())
        return;

    // In theory beginInsertRows() should suffice, but that fails to have an effect
    // if rowCount() returned 0 earlier.
    emit layoutAboutToBeChanged();

    foreach (const SftpFileInfo &fileInfo, filteredList) {
        SftpFileNode *childNode;
        if (fileInfo.type == FileTypeDirectory)
            childNode = new SftpDirNode;
        else
            childNode = new SftpFileNode;
        childNode->path = parentNode->path;
        if (!childNode->path.endsWith(QLatin1Char('/')))
            childNode->path += QLatin1Char('/');
        childNode->path += fileInfo.name;
        childNode->fileInfo = fileInfo;
        childNode->parent = parentNode;
        parentNode->children << childNode;
    }
    emit layoutChanged(); // Should be endInsertRows(), see above.
}

void SftpFileSystemModel::handleSftpJobFinished(SftpJobId jobId, const QString &errorMessage)
{
    if (jobId == d->statJobId) {
        d->statJobId = SftpInvalidJob;
        if (!errorMessage.isEmpty())
            emit sftpOperationFailed(tr("Error getting 'stat' info about '%1': %2")
                .arg(rootDirectory(), errorMessage));
        return;
    }

    DirNodeHash::Iterator it = d->lsOps.find(jobId);
    if (it != d->lsOps.end()) {
        QTC_CHECK(it.value()->lsState == SftpDirNode::LsRunning);
        it.value()->lsState = SftpDirNode::LsFinished;
        if (!errorMessage.isEmpty())
            emit sftpOperationFailed(tr("Error listing contents of directory '%1': %2")
                .arg(it.value()->path, errorMessage));
        d->lsOps.erase(it);
        return;
    }

    const int jobIndex = d->externalJobs.indexOf(jobId);
    QTC_ASSERT(jobIndex != -1, return);
    d->externalJobs.removeAt(jobIndex);
    emit sftpOperationFinished(jobId, errorMessage);
}

} // namespace Utils
