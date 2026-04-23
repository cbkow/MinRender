#pragma once

#include "core/peer_info.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>

#include <vector>

namespace MR {

// List model projection of PeerManager::getPeerSnapshot(). Excludes the
// local node — the NodePanel will surface "this node" separately via
// AppBridge properties, per the plan's Phase 4 layout.
//
// Updated from AppBridge::refresh() on the UI thread. Same fast-path /
// slow-path pattern as JobsModel: dataChanged per row when the set of
// nodeIds hasn't shifted; beginResetModel otherwise.
class NodesModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        NodeIdRole = Qt::UserRole + 1,
        HostnameRole,
        IsLeaderRole,
        IsActiveRole,          // node_state == "active" && is_alive
        IsAliveRole,
        AgentHealthRole,       // "ok" | "reconnecting" | "needs_attention"
        AlertReasonRole,
        TagsRole,              // QStringList
        PriorityRole,
        LastSeenRole,          // qint64 ms since epoch
        IsThisNodeRole,        // PeerInfo::is_local — false for snapshot peers
        EndpointRole,
        RenderStateRole,
        ActiveJobRole,
    };

    explicit NodesModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPeers(const std::vector<PeerInfo>& peers);

private:
    std::vector<PeerInfo> m_peers;
};

} // namespace MR
