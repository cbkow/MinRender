#include "ui/models/nodes_model.h"

#include <QString>
#include <QStringList>

namespace MR {

namespace {

bool sameIdAndOrder(const std::vector<PeerInfo>& a, const std::vector<PeerInfo>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].node_id != b[i].node_id)
            return false;
    }
    return true;
}

bool peerSameFields(const PeerInfo& a, const PeerInfo& b)
{
    return a.hostname       == b.hostname
        && a.node_state     == b.node_state
        && a.render_state   == b.render_state
        && a.active_job     == b.active_job
        && a.active_chunk   == b.active_chunk
        && a.priority       == b.priority
        && a.tags           == b.tags
        && a.endpoint       == b.endpoint
        && a.agent_health   == b.agent_health
        && a.alert_reason   == b.alert_reason
        && a.ready_for_work == b.ready_for_work
        && a.is_alive       == b.is_alive
        && a.is_leader      == b.is_leader
        && a.is_local       == b.is_local
        && a.last_seen_ms   == b.last_seen_ms;
}

} // namespace

NodesModel::NodesModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int NodesModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_peers.size());
}

QVariant NodesModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_peers.size()))
        return {};

    const PeerInfo& p = m_peers[static_cast<size_t>(index.row())];

    switch (role)
    {
    case NodeIdRole:       return QString::fromStdString(p.node_id);
    case HostnameRole:     return QString::fromStdString(p.hostname);
    case IsLeaderRole:     return p.is_leader;
    case IsActiveRole:     return p.is_alive && p.node_state == "active";
    case IsAliveRole:      return p.is_alive;
    case AgentHealthRole:  return QString::fromStdString(p.agent_health);
    case AlertReasonRole:  return QString::fromStdString(p.alert_reason);
    case TagsRole:
    {
        QStringList qs;
        qs.reserve(static_cast<int>(p.tags.size()));
        for (const auto& t : p.tags)
            qs.push_back(QString::fromStdString(t));
        return qs;
    }
    case PriorityRole:     return p.priority;
    case LastSeenRole:     return static_cast<qint64>(p.last_seen_ms);
    case IsThisNodeRole:   return p.is_local;
    case EndpointRole:     return QString::fromStdString(p.endpoint);
    case RenderStateRole:  return QString::fromStdString(p.render_state);
    case ActiveJobRole:    return QString::fromStdString(p.active_job);
    }
    return {};
}

QHash<int, QByteArray> NodesModel::roleNames() const
{
    return {
        { NodeIdRole,      "nodeId" },
        { HostnameRole,    "hostname" },
        { IsLeaderRole,    "isLeader" },
        { IsActiveRole,    "isActive" },
        { IsAliveRole,     "isAlive" },
        { AgentHealthRole, "agentHealth" },
        { AlertReasonRole, "alertReason" },
        { TagsRole,        "tags" },
        { PriorityRole,    "priority" },
        { LastSeenRole,    "lastSeen" },
        { IsThisNodeRole,  "isThisNode" },
        { EndpointRole,    "endpoint" },
        { RenderStateRole, "renderState" },
        { ActiveJobRole,   "activeJob" },
    };
}

void NodesModel::setPeers(const std::vector<PeerInfo>& incoming)
{
    if (sameIdAndOrder(m_peers, incoming))
    {
        for (size_t i = 0; i < m_peers.size(); ++i)
        {
            if (!peerSameFields(m_peers[i], incoming[i]))
            {
                m_peers[i] = incoming[i];
                const QModelIndex idx = index(static_cast<int>(i));
                emit dataChanged(idx, idx);
            }
        }
        return;
    }

    beginResetModel();
    m_peers = incoming;
    endResetModel();
}

} // namespace MR
