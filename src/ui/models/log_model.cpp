#include "ui/models/log_model.h"

#include "core/monitor_log.h"

namespace MR {

LogModel::LogModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

LogModel::~LogModel()
{
    // setCallback(nullptr) acquires MonitorLog's mutex, which blocks
    // until any in-flight callback invocation completes. After this call,
    // no new invokeMethod posts can race our destruction.
    MonitorLog::instance().setCallback(nullptr);
}

int LogModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const Row& r = m_rows[static_cast<size_t>(index.row())];
    switch (role)
    {
    case TimestampRole: return r.timestamp;
    case LevelRole:     return r.level;
    case CategoryRole:  return r.category;
    case MessageRole:   return r.message;
    }
    return {};
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
        { TimestampRole, "timestamp" },
        { LevelRole,     "level" },
        { CategoryRole,  "category" },
        { MessageRole,   "message" },
    };
}

void LogModel::attach()
{
    // Seed from MonitorLog's current buffer. Entries logged between
    // getEntries() and setCallback() below fall into a brief race window;
    // acceptable for a view that tolerates occasional drops at the seam.
    const auto existing = MonitorLog::instance().getEntries();
    if (!existing.empty())
    {
        beginResetModel();
        m_rows.clear();
        for (const auto& e : existing)
        {
            Row r;
            r.timestamp = e.timestamp_ms;
            r.level     = QString::fromStdString(e.level);
            r.category  = QString::fromStdString(e.category);
            r.message   = QString::fromStdString(e.message);
            m_rows.push_back(std::move(r));
            if (static_cast<int>(m_rows.size()) > kCapacity)
                m_rows.pop_front();
        }
        endResetModel();
    }

    // Install the callback. Note: this overwrites MonitorApp's existing
    // UI-IPC push callback — intentionally, per the Phase 7 plan to
    // delete src/core/ui_ipc_server entirely. No other MonitorLog client
    // remains.
    MonitorLog::instance().setCallback(
        [this](const MonitorLog::Entry& e)
        {
            QMetaObject::invokeMethod(
                this, "appendOnUiThread", Qt::QueuedConnection,
                Q_ARG(qint64, e.timestamp_ms),
                Q_ARG(QString, QString::fromStdString(e.level)),
                Q_ARG(QString, QString::fromStdString(e.category)),
                Q_ARG(QString, QString::fromStdString(e.message)));
        });
}

void LogModel::clear()
{
    if (m_rows.empty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void LogModel::appendOnUiThread(qint64 timestampMs,
                                const QString& level,
                                const QString& category,
                                const QString& message)
{
    if (static_cast<int>(m_rows.size()) >= kCapacity)
    {
        beginRemoveRows({}, 0, 0);
        m_rows.pop_front();
        endRemoveRows();
    }

    const int row = static_cast<int>(m_rows.size());
    beginInsertRows({}, row, row);
    m_rows.push_back({ timestampMs, level, category, message });
    endInsertRows();
}

} // namespace MR
