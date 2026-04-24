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

    // QML binds these from Theme.qml so the colour scheme swaps without
    // a C++ rebuild. Defaults match the Tokyo-Night-ish palette the rest
    // of the app uses, so the grid still looks right if left unbound.
    Q_PROPERTY(QColor bgColor        READ bgColor        WRITE setBgColor        NOTIFY bgColorChanged)
    Q_PROPERTY(QColor unclaimedColor READ unclaimedColor WRITE setUnclaimedColor NOTIFY unclaimedColorChanged)
    Q_PROPERTY(QColor assignedColor  READ assignedColor  WRITE setAssignedColor  NOTIFY assignedColorChanged)
    Q_PROPERTY(QColor completedColor READ completedColor WRITE setCompletedColor NOTIFY completedColorChanged)
    Q_PROPERTY(QColor failedColor    READ failedColor    WRITE setFailedColor    NOTIFY failedColorChanged)

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

    QColor bgColor() const { return m_bg; }
    void setBgColor(const QColor& c);
    QColor unclaimedColor() const { return m_unclaimed; }
    void setUnclaimedColor(const QColor& c);
    QColor assignedColor() const { return m_assigned; }
    void setAssignedColor(const QColor& c);
    QColor completedColor() const { return m_completed; }
    void setCompletedColor(const QColor& c);
    QColor failedColor() const { return m_failed; }
    void setFailedColor(const QColor& c);

    void paint(QPainter* painter) override;

signals:
    void modelChanged();
    void frameStartChanged();
    void frameEndChanged();
    void cellSizeChanged();
    void bgColorChanged();
    void unclaimedColorChanged();
    void assignedColorChanged();
    void completedColorChanged();
    void failedColorChanged();

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

    QColor m_bg        { 0x10, 0x10, 0x10 };
    QColor m_unclaimed { 0x30, 0x30, 0x30 };
    QColor m_assigned  { 0x7a, 0xa2, 0xf7 };
    QColor m_completed { 0x9e, 0xce, 0x6a };
    QColor m_failed    { 0xf7, 0x76, 0x8e };
};

} // namespace MR
