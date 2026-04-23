#pragma once

#include "core/job_types.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>

#include <vector>

namespace MR {

// List model projection of MonitorApp::cachedJobs(). Owned by AppBridge
// and updated from AppBridge::refresh() on the UI thread — the caller is
// responsible for thread-affinity (refreshCachedJobs runs on the 50 ms
// QTimer tick, same thread as the model).
//
// Updates take a fast path when the set of job IDs and their order is
// unchanged (the common case — progress ticks): only dataChanged emits,
// so QML delegates don't get recreated. Otherwise a full reset.
class JobsModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        JobIdRole = Qt::UserRole + 1,
        NameRole,
        SlugRole,
        StateRole,
        ProgressRole,         // double in [0, 1]
        TotalChunksRole,
        DoneChunksRole,
        FailedChunksRole,
        RenderingChunksRole,
        CreatedAtRole,        // qint64 ms since epoch
        PriorityRole,
    };

    explicit JobsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setJobs(const std::vector<JobInfo>& jobs);

private:
    std::vector<JobInfo> m_jobs;
};

} // namespace MR
