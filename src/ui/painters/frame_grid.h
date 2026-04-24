#pragma once

#include <QQuickPaintedItem>

#include "ui/models/chunks_model.h"

namespace MR {

// QQuickPaintedItem that renders one cell per frame in the selected
// job's range, coloured by chunk state (pending / assigned / completed /
// failed) with per-frame overrides from ChunkRow::completed_frames.
//
// Repaints fire only in response to ChunksModel signals (dataChanged,
// rowsInserted, rowsRemoved, modelReset) or property changes — not on
// a timer. That's the plan's "zero measurable GPU/CPU at idle" rule.
class FrameGrid : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(MR::ChunksModel* model      READ model      WRITE setModel      NOTIFY modelChanged)
    Q_PROPERTY(int              frameStart READ frameStart WRITE setFrameStart NOTIFY frameStartChanged)
    Q_PROPERTY(int              frameEnd   READ frameEnd   WRITE setFrameEnd   NOTIFY frameEndChanged)
    Q_PROPERTY(int              cellSize   READ cellSize   WRITE setCellSize   NOTIFY cellSizeChanged)

public:
    explicit FrameGrid(QQuickItem* parent = nullptr);

    ChunksModel* model() const { return m_model; }
    void setModel(ChunksModel* m);

    int frameStart() const { return m_frameStart; }
    void setFrameStart(int s);

    int frameEnd() const { return m_frameEnd; }
    void setFrameEnd(int e);

    int cellSize() const { return m_cellSize; }
    void setCellSize(int s);

    void paint(QPainter* painter) override;

signals:
    void modelChanged();
    void frameStartChanged();
    void frameEndChanged();
    void cellSizeChanged();

private:
    void reconnectModel();

    ChunksModel* m_model = nullptr;
    QMetaObject::Connection m_dataChangedConn;
    QMetaObject::Connection m_rowsInsertedConn;
    QMetaObject::Connection m_rowsRemovedConn;
    QMetaObject::Connection m_modelResetConn;

    int m_frameStart = 0;
    int m_frameEnd   = 0;
    int m_cellSize   = 8;
};

} // namespace MR
