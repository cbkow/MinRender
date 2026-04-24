#include "ui/painters/frame_grid.h"

#include <QColor>
#include <QPainter>
#include <QVariantList>

#include <algorithm>

namespace MR {

// Frame state colour table. Phase 6 will pull these from Theme.qml so a
// user-tunable palette can apply without a C++ rebuild.
namespace {
const QColor kBgColor        (0x10, 0x10, 0x10);
const QColor kUnclaimedColor (0x30, 0x30, 0x30);
const QColor kAssignedColor  (0x7a, 0xa2, 0xf7);   // blue
const QColor kCompletedColor (0x9e, 0xce, 0x6a);   // green
const QColor kFailedColor    (0xf7, 0x76, 0x8e);   // red
}  // namespace

FrameGrid::FrameGrid(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
    // Repaint the whole item whenever size changes so the grid re-lays-out.
    connect(this, &QQuickPaintedItem::widthChanged,
            this, [this]() { update(); });
    connect(this, &QQuickPaintedItem::heightChanged,
            this, [this]() { update(); });
}

void FrameGrid::setModel(ChunksModel* m)
{
    if (m_model == m)
        return;

    QObject::disconnect(m_dataChangedConn);
    QObject::disconnect(m_rowsInsertedConn);
    QObject::disconnect(m_rowsRemovedConn);
    QObject::disconnect(m_modelResetConn);

    m_model = m;
    reconnectModel();
    emit modelChanged();
    update();
}

void FrameGrid::reconnectModel()
{
    if (!m_model) return;

    // Any structural or data change on the model → repaint. update() on
    // QQuickPaintedItem just schedules a single repaint on the render
    // thread; multiple calls within one event-loop pass coalesce.
    m_dataChangedConn = connect(
        m_model, &QAbstractItemModel::dataChanged,
        this, [this]() { update(); });
    m_rowsInsertedConn = connect(
        m_model, &QAbstractItemModel::rowsInserted,
        this, [this]() { update(); });
    m_rowsRemovedConn = connect(
        m_model, &QAbstractItemModel::rowsRemoved,
        this, [this]() { update(); });
    m_modelResetConn = connect(
        m_model, &QAbstractItemModel::modelReset,
        this, [this]() { update(); });
}

void FrameGrid::setFrameStart(int s)
{
    if (m_frameStart == s) return;
    m_frameStart = s;
    emit frameStartChanged();
    update();
}

void FrameGrid::setFrameEnd(int e)
{
    if (m_frameEnd == e) return;
    m_frameEnd = e;
    emit frameEndChanged();
    update();
}

void FrameGrid::setCellSize(int s)
{
    if (s <= 0 || m_cellSize == s) return;
    m_cellSize = s;
    emit cellSizeChanged();
    update();
}

void FrameGrid::paint(QPainter* painter)
{
    const QRectF area = boundingRect();
    painter->fillRect(area, kBgColor);

    if (!m_model || m_frameEnd < m_frameStart || m_cellSize <= 0)
        return;

    const int totalFrames = m_frameEnd - m_frameStart + 1;
    const int cellsPerRow = std::max(
        1, static_cast<int>(area.width()) / m_cellSize);
    const int padding     = 1;   // 1 px gap gives visible separation
    const int drawSize    = m_cellSize - 2 * padding;
    if (drawSize <= 0)
        return;

    auto cellRect = [&](int frameIdx) -> QRect {
        const int row = frameIdx / cellsPerRow;
        const int col = frameIdx % cellsPerRow;
        return { col * m_cellSize + padding,
                 row * m_cellSize + padding,
                 drawSize, drawSize };
    };

    // Pass 1: fill every frame cell with the unclaimed colour. This is
    // cheap enough at 10 k frames because each fillRect is a solid
    // rect with no state churn; Qt batches them via the raster backend.
    // It also ensures gaps (frames not covered by any chunk) are
    // visibly distinct from the background.
    for (int i = 0; i < totalFrames; ++i)
        painter->fillRect(cellRect(i), kUnclaimedColor);

    // Pass 2: for each chunk, paint its frame range in the chunk's
    // colour, then overpaint completed_frames in green so partial
    // progress through an assigned chunk shows through.
    const int rowCount = m_model->rowCount();
    for (int r = 0; r < rowCount; ++r)
    {
        const QModelIndex idx = m_model->index(r, 0);
        const int fs = m_model->data(idx, ChunksModel::FrameStartRole).toInt();
        const int fe = m_model->data(idx, ChunksModel::FrameEndRole).toInt();
        const QString state =
            m_model->data(idx, ChunksModel::StateRole).toString();

        QColor chunkColor = kUnclaimedColor;
        if (state == QStringLiteral("completed"))
            chunkColor = kCompletedColor;
        else if (state == QStringLiteral("failed"))
            chunkColor = kFailedColor;
        else if (state == QStringLiteral("assigned")
              || state == QStringLiteral("rendering"))
            chunkColor = kAssignedColor;
        // "pending" stays unclaimed (already painted in pass 1).

        if (chunkColor != kUnclaimedColor)
        {
            const int firstIdx = std::max(0, fs - m_frameStart);
            const int lastIdx  = std::min(totalFrames - 1, fe - m_frameStart);
            for (int i = firstIdx; i <= lastIdx; ++i)
                painter->fillRect(cellRect(i), chunkColor);
        }

        // Completed frames inside this chunk override — even if the
        // chunk state is still "assigned" they're proof of progress.
        const QVariantList done =
            m_model->data(idx, ChunksModel::CompletedFramesRole).toList();
        for (const QVariant& v : done)
        {
            const int f = v.toInt();
            const int i = f - m_frameStart;
            if (i < 0 || i >= totalFrames)
                continue;
            painter->fillRect(cellRect(i), kCompletedColor);
        }
    }
}

} // namespace MR
