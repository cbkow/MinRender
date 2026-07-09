#include "ui/painters/frame_grid.h"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QVariantList>

#include <algorithm>

namespace MR {

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
    updateImplicitHeight();
    update();
}

void FrameGrid::setFrameEnd(int e)
{
    if (m_frameEnd == e) return;
    m_frameEnd = e;
    emit frameEndChanged();
    updateImplicitHeight();
    update();
}

void FrameGrid::setCellSize(int s)
{
    if (s <= 0 || m_cellSize == s) return;
    m_cellSize = s;
    emit cellSizeChanged();
    updateImplicitHeight();
    update();
}

void FrameGrid::setSelectedFrame(int f)
{
    if (m_selectedFrame == f) return;
    m_selectedFrame = f;
    emit selectedFrameChanged();
    update();
}

int FrameGrid::frameAtPosition(qreal x, qreal y) const
{
    const int totalFrames = m_frameEnd - m_frameStart + 1;
    if (totalFrames <= 0 || m_cellSize <= 0 || x < 0 || y < 0)
        return -1;
    const int cellsPerRow = std::max(1, static_cast<int>(width()) / m_cellSize);
    const int col = static_cast<int>(x) / m_cellSize;
    const int row = static_cast<int>(y) / m_cellSize;
    if (col >= cellsPerRow)
        return -1;
    const int idx = row * cellsPerRow + col;
    if (idx < 0 || idx >= totalFrames)
        return -1;
    return m_frameStart + idx;
}

void FrameGrid::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (!qFuzzyCompare(newGeometry.width(), oldGeometry.width()))
        updateImplicitHeight();
}

void FrameGrid::updateImplicitHeight()
{
    const int totalFrames = m_frameEnd - m_frameStart + 1;
    if (totalFrames <= 0 || m_cellSize <= 0)
    {
        setImplicitHeight(0);
        return;
    }
    const int cols = std::max(1, std::max(1, static_cast<int>(width())) / m_cellSize);
    const int rows = (totalFrames + cols - 1) / cols;
    setImplicitHeight(rows * m_cellSize);
}

void FrameGrid::setBgColor(const QColor& c)
{
    if (m_bg == c) return;
    m_bg = c; emit bgColorChanged(); update();
}
void FrameGrid::setUnclaimedColor(const QColor& c)
{
    if (m_unclaimed == c) return;
    m_unclaimed = c; emit unclaimedColorChanged(); update();
}
void FrameGrid::setAssignedColor(const QColor& c)
{
    if (m_assigned == c) return;
    m_assigned = c; emit assignedColorChanged(); update();
}
void FrameGrid::setRenderedColor(const QColor& c)
{
    if (m_rendered == c) return;
    m_rendered = c; emit renderedColorChanged(); update();
}
void FrameGrid::setCompletedColor(const QColor& c)
{
    if (m_completed == c) return;
    m_completed = c; emit completedColorChanged(); update();
}
void FrameGrid::setFailedColor(const QColor& c)
{
    if (m_failed == c) return;
    m_failed = c; emit failedColorChanged(); update();
}

void FrameGrid::paint(QPainter* painter)
{
    const QRectF area = boundingRect();
    painter->fillRect(area, m_bg);

    if (!m_model || m_frameEnd < m_frameStart || m_cellSize <= 0)
        return;

    const int totalFrames = m_frameEnd - m_frameStart + 1;
    const int cell        = m_cellSize;
    const int cellsPerRow = std::max(
        1, static_cast<int>(area.width()) / cell);
    const int padding     = 1;   // 1 px gap gives visible separation
    const int drawSize    = cell - 2 * padding;
    if (drawSize <= 0)
        return;

    auto cellRect = [&](int frameIdx) -> QRect {
        const int row = frameIdx / cellsPerRow;
        const int col = frameIdx % cellsPerRow;
        return { col * cell + padding,
                 row * cell + padding,
                 drawSize, drawSize };
    };

    // Pass 1: fill every frame cell with the unclaimed colour. This is
    // cheap enough at 10 k frames because each fillRect is a solid
    // rect with no state churn; Qt batches them via the raster backend.
    // It also ensures gaps (frames not covered by any chunk) are
    // visibly distinct from the background.
    for (int i = 0; i < totalFrames; ++i)
        painter->fillRect(cellRect(i), m_unclaimed);

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

        QColor chunkColor = m_unclaimed;
        if (state == QStringLiteral("completed"))
            chunkColor = m_completed;
        else if (state == QStringLiteral("failed"))
            chunkColor = m_failed;
        else if (state == QStringLiteral("assigned")
              || state == QStringLiteral("rendering"))
            chunkColor = m_assigned;
        // "pending" stays unclaimed (already painted in pass 1).

        if (chunkColor != m_unclaimed)
        {
            const int firstIdx = std::max(0, fs - m_frameStart);
            const int lastIdx  = std::min(totalFrames - 1, fe - m_frameStart);
            for (int i = firstIdx; i <= lastIdx; ++i)
                painter->fillRect(cellRect(i), chunkColor);
        }

        // Completed frames inside this chunk override the chunk's base
        // colour. If the chunk itself is "completed" (files have been
        // copied to the destination), every frame is shown in the bright
        // completedColor — chunkColor === m_completed already, so this
        // pass is a no-op for those. Otherwise the frame was rendered
        // locally and is still in staging awaiting copy: draw it in the
        // dimmer renderedColor so the user can tell rendered-but-not-
        // copied apart from fully-finished work.
        const QColor frameDoneColor =
            (state == QStringLiteral("completed")) ? m_completed : m_rendered;
        const QVariantList done =
            m_model->data(idx, ChunksModel::CompletedFramesRole).toList();
        for (const QVariant& v : done)
        {
            const int f = v.toInt();
            const int i = f - m_frameStart;
            if (i < 0 || i >= totalFrames)
                continue;
            painter->fillRect(cellRect(i), frameDoneColor);
        }
    }

    // Selection outline (preview pin) — drawn last so it sits on top of
    // every state colour. Covers the full cell including the gutter so
    // it reads at 10px cells.
    if (m_selectedFrame >= m_frameStart && m_selectedFrame <= m_frameEnd)
    {
        const int idx = m_selectedFrame - m_frameStart;
        const int row = idx / cellsPerRow;
        const int col = idx % cellsPerRow;
        painter->setPen(QPen(QColor(0xff, 0xff, 0xff), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(col * cell, row * cell, cell - 1, cell - 1);
    }
}

} // namespace MR
