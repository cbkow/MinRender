#include "ui/models/jobs_model.h"

#include <QString>

namespace MR {

namespace {

bool sameIdAndOrder(const std::vector<JobInfo>& a, const std::vector<JobInfo>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].manifest.job_id != b[i].manifest.job_id)
            return false;
    }
    return true;
}

bool jobsSameFields(const JobInfo& a, const JobInfo& b)
{
    return a.current_state       == b.current_state
        && a.current_priority    == b.current_priority
        && a.total_chunks        == b.total_chunks
        && a.completed_chunks    == b.completed_chunks
        && a.failed_chunks       == b.failed_chunks
        && a.rendering_chunks    == b.rendering_chunks;
}

} // namespace

JobsModel::JobsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int JobsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_jobs.size());
}

QVariant JobsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_jobs.size()))
        return {};

    const JobInfo& job = m_jobs[static_cast<size_t>(index.row())];

    switch (role)
    {
    case JobIdRole:
    case SlugRole:
        // `slug` and `jobId` are the same string today; the distinction
        // exists in the plan to leave room for a human-friendly name
        // separate from the directory slug if that ever splits.
        return QString::fromStdString(job.manifest.job_id);
    case NameRole:
        // No dedicated display name in JobInfo; use the slug until Phase 4
        // decides whether to derive one (e.g. from template + submitter).
        return QString::fromStdString(job.manifest.job_id);
    case StateRole:
        return QString::fromStdString(job.current_state);
    case ProgressRole:
    {
        if (job.total_chunks <= 0)
            return 0.0;
        return static_cast<double>(job.completed_chunks)
             / static_cast<double>(job.total_chunks);
    }
    case TotalChunksRole:
        return job.total_chunks;
    case DoneChunksRole:
        return job.completed_chunks;
    case FailedChunksRole:
        return job.failed_chunks;
    case RenderingChunksRole:
        return job.rendering_chunks;
    case CreatedAtRole:
        return static_cast<qint64>(job.manifest.submitted_at_ms);
    case PriorityRole:
        return job.current_priority;
    }
    return {};
}

QHash<int, QByteArray> JobsModel::roleNames() const
{
    return {
        { JobIdRole,           "jobId" },
        { NameRole,            "name" },
        { SlugRole,            "slug" },
        { StateRole,           "state" },
        { ProgressRole,        "progress" },
        { TotalChunksRole,     "totalChunks" },
        { DoneChunksRole,      "doneChunks" },
        { FailedChunksRole,    "failedChunks" },
        { RenderingChunksRole, "renderingChunks" },
        { CreatedAtRole,       "createdAt" },
        { PriorityRole,        "priority" },
    };
}

void JobsModel::setJobs(const std::vector<JobInfo>& incoming)
{
    // Fast path: same IDs in same order. Only progress/state fields may
    // have changed — emit dataChanged per dirty row so QML delegates stay
    // mounted (selection/animations preserved).
    if (sameIdAndOrder(m_jobs, incoming))
    {
        for (size_t i = 0; i < m_jobs.size(); ++i)
        {
            if (!jobsSameFields(m_jobs[i], incoming[i]))
            {
                m_jobs[i] = incoming[i];
                const QModelIndex idx = index(static_cast<int>(i));
                emit dataChanged(idx, idx);
            }
        }
        return;
    }

    // Slow path: set membership or order changed — full reset. A smarter
    // insert/remove diff can land in a later phase if delegate churn
    // becomes a problem.
    beginResetModel();
    m_jobs = incoming;
    endResetModel();
}

} // namespace MR
