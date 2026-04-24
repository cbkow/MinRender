#include "ui/models/chunks_model.h"

#include <QString>
#include <QVariantList>

namespace MR {

namespace {

bool sameIdAndOrder(const std::vector<ChunkRow>& a, const std::vector<ChunkRow>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].id != b[i].id)
            return false;
    }
    return true;
}

bool chunkSameFields(const ChunkRow& a, const ChunkRow& b)
{
    return a.state            == b.state
        && a.assigned_to      == b.assigned_to
        && a.assigned_at_ms   == b.assigned_at_ms
        && a.completed_at_ms  == b.completed_at_ms
        && a.retry_count      == b.retry_count
        && a.completed_frames == b.completed_frames;
}

} // namespace

ChunksModel::ChunksModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ChunksModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_chunks.size());
}

QVariant ChunksModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_chunks.size()))
        return {};

    const ChunkRow& c = m_chunks[static_cast<size_t>(index.row())];

    switch (role)
    {
    case ChunkIdRole:      return static_cast<qint64>(c.id);
    case FrameStartRole:   return c.frame_start;
    case FrameEndRole:     return c.frame_end;
    case StateRole:        return QString::fromStdString(c.state);
    case AssignedNodeRole: return QString::fromStdString(c.assigned_to);
    case ProgressRole:
    {
        const int total = c.frame_end - c.frame_start + 1;
        if (total <= 0)
            return 0.0;
        return static_cast<double>(c.completed_frames.size())
             / static_cast<double>(total);
    }
    case AssignedAtRole:   return static_cast<qint64>(c.assigned_at_ms);
    case CompletedAtRole:  return static_cast<qint64>(c.completed_at_ms);
    case RetryCountRole:   return c.retry_count;
    case CompletedFramesRole:
    {
        QVariantList frames;
        frames.reserve(static_cast<int>(c.completed_frames.size()));
        for (int f : c.completed_frames)
            frames.push_back(f);
        return frames;
    }
    }
    return {};
}

QHash<int, QByteArray> ChunksModel::roleNames() const
{
    return {
        { ChunkIdRole,      "chunkId" },
        { FrameStartRole,   "frameStart" },
        { FrameEndRole,     "frameEnd" },
        { StateRole,        "state" },
        { AssignedNodeRole, "assignedNode" },
        { ProgressRole,     "progress" },
        { AssignedAtRole,      "assignedAt" },
        { CompletedAtRole,     "completedAt" },
        { RetryCountRole,      "retryCount" },
        { CompletedFramesRole, "completedFrames" },
    };
}

void ChunksModel::setChunks(const std::vector<ChunkRow>& incoming)
{
    if (sameIdAndOrder(m_chunks, incoming))
    {
        for (size_t i = 0; i < m_chunks.size(); ++i)
        {
            if (!chunkSameFields(m_chunks[i], incoming[i]))
            {
                m_chunks[i] = incoming[i];
                const QModelIndex idx = index(static_cast<int>(i));
                emit dataChanged(idx, idx);
            }
        }
        return;
    }

    beginResetModel();
    m_chunks = incoming;
    endResetModel();
}

void ChunksModel::clear()
{
    if (m_chunks.empty())
        return;
    beginResetModel();
    m_chunks.clear();
    endResetModel();
}

} // namespace MR
