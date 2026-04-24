#pragma once

#include "monitor/database_manager.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>

#include <vector>

namespace MR {

// Per-job chunk list model. AppBridge owns it and drives refreshes on
// a 3 s timer while a job is selected. On worker nodes, the underlying
// MonitorApp::getChunksForJob does a blocking HTTP GET to the leader
// (up to ~1.5 s) — acceptable for Phase 3 wiring; Phase 5's frame grid
// will revisit if the cadence becomes a UX problem.
class ChunksModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        ChunkIdRole = Qt::UserRole + 1,
        FrameStartRole,
        FrameEndRole,
        StateRole,              // "pending" | "assigned" | "completed" | "failed"
        AssignedNodeRole,
        ProgressRole,           // double in [0, 1] — completed_frames / total in chunk
        AssignedAtRole,         // qint64 ms since epoch
        CompletedAtRole,        // qint64 ms since epoch
        RetryCountRole,
        CompletedFramesRole,    // QVariantList<int> — individual frames done inside the chunk
    };

    explicit ChunksModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setChunks(const std::vector<ChunkRow>& chunks);
    void clear();

private:
    std::vector<ChunkRow> m_chunks;
};

} // namespace MR
