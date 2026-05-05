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
    // Frame rendered locally / sitting in staging — not yet copied to
    // the destination. Drawn over `assignedColor` for individual frames
    // listed in completed_frames while the chunk state is still
    // assigned/rendering. Distinct from `completedColor` so the user can
    // tell when a chunk has finished rendering but the post-render copy
    // is still in flight.
    Q_PROPERTY(QColor renderedColor  READ renderedColor  WRITE setRenderedColor  NOTIFY renderedColorChanged)
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
    QColor renderedColor() const { return m_rendered; }
    void setRenderedColor(const QColor& c);
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
    void renderedColorChanged();
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
    QColor m_assigned  { 0x01, 0x89, 0xf1 };
    QColor m_rendered  { 0x2a, 0x82, 0x28 };  // dark green — staged, awaiting copy
    QColor m_completed { 0x46, 0xc8, 0x46 };  // light green — chunk fully copied to destination
    QColor m_failed    { 0xc0, 0x40, 0x40 };
};

} // namespace MR
