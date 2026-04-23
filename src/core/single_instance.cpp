#include "core/single_instance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QObject>
#include <QStandardPaths>
#include <QString>

namespace MR {

namespace {

QString lockFilePath(const std::string& name)
{
    QString temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return temp + QStringLiteral("/") + QString::fromStdString(name) + QStringLiteral(".lock");
}

QString serverName(const std::string& name)
{
    return QString::fromStdString(name);
}

} // namespace

SingleInstance::SingleInstance(const std::string& name)
    : m_name(name)
    , m_lock(std::make_unique<QLockFile>(lockFilePath(name)))
{
    // Strict mutual exclusion — don't auto-steal the lock if the previous
    // holder crashed. QLockFile cleans up the file on unlock in the dtor;
    // if a prior process crashed without unlocking, the user (or installer)
    // can remove the .lock file manually.
    m_lock->setStaleLockTime(0);
    m_isFirst = m_lock->tryLock(0);

    if (!m_isFirst)
        return;

    // Clean any stale local-server endpoint left over from a crashed prior
    // run. Safe: we already hold the lock so nothing else is listening.
    QLocalServer::removeServer(serverName(name));

    m_server = std::make_unique<QLocalServer>();
    if (m_server->listen(serverName(name)))
    {
        QObject::connect(m_server.get(), &QLocalServer::newConnection,
            [this]() {
                while (QLocalSocket* socket = m_server->nextPendingConnection())
                {
                    socket->deleteLater();
                    if (m_activationCb)
                        m_activationCb();
                }
            });
    }
}

SingleInstance::~SingleInstance()
{
    if (m_server)
    {
        m_server->close();
        m_server.reset();
    }
    if (m_lock)
    {
        m_lock->unlock();
        m_lock.reset();
    }
}

bool SingleInstance::isFirst() const
{
    return m_isFirst;
}

void SingleInstance::signalExisting()
{
    if (m_isFirst)
        return;

    QLocalSocket socket;
    socket.connectToServer(serverName(m_name));
    if (socket.waitForConnected(500))
    {
        socket.write("show", 4);
        socket.flush();
        socket.waitForBytesWritten(200);
        socket.disconnectFromServer();
    }
}

void SingleInstance::setActivationCallback(std::function<void()> cb)
{
    m_activationCb = std::move(cb);
}

} // namespace MR
