#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QString>

#include <deque>

namespace MR {

// Ring-buffer log model. Takes over MonitorLog::setCallback (overwriting
// MonitorApp's old Tauri-UI-IPC callback that's slated for deletion in
// Phase 7). The callback fires on whatever thread produced the log entry;
// entries are marshalled to the UI thread via QMetaObject::invokeMethod
// (Qt::QueuedConnection) before touching any model state — the plan's
// load-bearing invariant.
//
// Capacity is UI-side. MonitorLog's internal ring is 1000; we keep more
// (5000, per the plan) so the LogPanel has meaningful scrollback across
// long farm sessions.
class LogModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        TimestampRole = Qt::UserRole + 1,   // qint64 ms since epoch
        LevelRole,                          // "INFO" | "WARN" | "ERROR"
        CategoryRole,
        MessageRole,
    };

    static constexpr int kCapacity = 5000;

    explicit LogModel(QObject* parent = nullptr);
    ~LogModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Seeds from MonitorLog's current ring-buffer snapshot and installs
    // the callback. Call once after MonitorApp::init() runs.
    void attach();

    Q_INVOKABLE void clear();

    // Declared Q_INVOKABLE so QMetaObject::invokeMethod's string-name
    // overload can dispatch across the queued connection from arbitrary
    // worker threads.
    Q_INVOKABLE void appendOnUiThread(qint64 timestampMs,
                                      const QString& level,
                                      const QString& category,
                                      const QString& message);

private:
    struct Row
    {
        qint64 timestamp = 0;
        QString level;
        QString category;
        QString message;
    };

    std::deque<Row> m_rows;
};

} // namespace MR
