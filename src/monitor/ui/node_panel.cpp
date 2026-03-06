#include "monitor/ui/node_panel.h"
#include "monitor/ui/style.h"
#include "monitor/ui/ui_macros.h"
#include "monitor/monitor_app.h"
#include "monitor/dispatch_manager.h"
#include "core/net_utils.h"
#include "core/http_server.h"

#include <imgui.h>
#include <httplib.h>
#include <algorithm>
#include <thread>
#include <fstream>

namespace MR {

void NodePanel::init(MonitorApp* app)
{
    m_app = app;
}

static void drawStatusBadge(const char* label, const ImVec4& color)
{
    ImGui::TextColored(color, "[%s]", label);
}

void NodePanel::render()
{
    if (!visible) return;

    if (ImGui::Begin("Node Overview", nullptr, ImGuiWindowFlags_NoTitleBar))
    {
        panelHeader("Nodes", Icons::Hub, visible);
        if (!m_app || !m_app->isFarmRunning())
        {
            ImGui::TextDisabled("Farm not connected.");
            ImGui::TextDisabled("Configure Sync Root in Settings.");

            if (m_app && m_app->hasFarmError())
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Error: %s",
                                   m_app->farmError().c_str());
            }
        }
        else
        {
            // --- Local node ---
            const auto& id = m_app->identity();
            PushOutlineHeaderStyle();
            bool thisNodeOpen = CollapsingHeaderWithIcon("      This Node", Fonts::icons, Icons::Monitor, getAccentColor(), ImGuiTreeNodeFlags_DefaultOpen);
            PopOutlineHeaderStyle();

            if (thisNodeOpen)
            {
            if (m_app->isLeader())
                drawStatusBadge("Leader", ImVec4(1.0f, 0.84f, 0.0f, 1.0f));

            ImGui::Spacing();
            ImGui::Text("ID: %s", id.nodeId().c_str());
            ImGui::Text("Host: %s", id.systemInfo().hostname.c_str());

            if (m_app->nodeState() == NodeState::Active)
            {
                if (m_app->renderCoordinator().isRendering())
                    drawStatusBadge("Rendering", ImVec4(0.3f, 0.5f, 1.0f, 1.0f));
                else
                    drawStatusBadge("Active", ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
            }
            else
            {
                drawStatusBadge("Stopped", ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            }

            // Local agent health badge
            {
                auto health = m_app->agentSupervisor().agentHealthEnum();
                if (health == AgentHealth::NeedsAttention)
                {
                    ImGui::SameLine();
                    drawStatusBadge("Agent Down", ImVec4(1.0f, 0.4f, 0.2f, 1.0f));
                }
            }

            if (m_app->renderCoordinator().isRendering())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s %s",
                    m_app->renderCoordinator().currentJobId().c_str(),
                    m_app->renderCoordinator().currentChunkLabel().c_str());
            }

            if (!id.systemInfo().gpuName.empty())
                ImGui::Text("GPU: %s", id.systemInfo().gpuName.c_str());
            if (id.systemInfo().cpuCores > 0)
                ImGui::Text("CPU: %d cores  |  RAM: %llu MB",
                    id.systemInfo().cpuCores,
                    static_cast<unsigned long long>(id.systemInfo().ramMB));

            // Node control
            ImGui::Spacing();
            PushOutlineButtonStyle();
            if (m_app->nodeState() == NodeState::Active)
            {
                if (ImGui::Button("Stop Node"))
                    m_app->setNodeState(NodeState::Stopped);
            }
            else
            {
                if (ImGui::Button("Start Node"))
                    m_app->setNodeState(NodeState::Active);
            }
            PopOutlineButtonStyle();

            } // end This Node

            // --- Peers ---
            PushOutlineHeaderStyle();
            bool peersOpen = CollapsingHeaderWithIcon("      Peers", Fonts::icons, Icons::Network, getAccentColor(), ImGuiTreeNodeFlags_DefaultOpen);
            PopOutlineHeaderStyle();

            auto peers = m_app->peerManager().getPeerSnapshot();

            if (!peersOpen)
            {
                // skip peer rendering
            }
            else if (peers.empty())
            {
                ImGui::TextDisabled("No peers discovered.");
            }
            else
            {
                // Sort: alive first (rendering > idle > stopped), dead last; alpha within
                std::sort(peers.begin(), peers.end(), [](const PeerInfo& a, const PeerInfo& b)
                {
                    if (a.is_alive != b.is_alive)
                        return a.is_alive > b.is_alive;

                    // Rendering > Idle > Stopped
                    auto stateOrder = [](const PeerInfo& p) -> int {
                        if (p.render_state == "rendering") return 0;
                        if (p.node_state == "active") return 1;
                        return 2;
                    };
                    int oa = stateOrder(a), ob = stateOrder(b);
                    if (oa != ob) return oa < ob;

                    return a.hostname < b.hostname;
                });

                for (const auto& peer : peers)
                {
                    ImGui::PushID(peer.node_id.c_str());

                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    ImVec2 group_start = ImGui::GetCursorScreenPos();
                    ImGui::BeginGroup();

                    ImGui::Dummy(ImVec2(0, 2.0f));  // top padding

                    // UDP connectivity icon
                    ImGui::Indent(8.0f);  // horizontal padding
                    if (Fonts::icons)
                    {
                        ImGui::PushFont(Fonts::icons);
                        if (peer.has_udp_contact)
                            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s", Icons::Wifi);
                        else
                            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.5f), "%s", Icons::WifiOff);
                        ImGui::PopFont();
                        ImGui::SameLine();
                    }

                    // Status badge
                    if (!peer.is_alive)
                    {
                        drawStatusBadge("Dead", ImVec4(0.4f, 0.4f, 0.4f, 0.7f));
                    }
                    else if (peer.node_state == "stopped")
                    {
                        drawStatusBadge("Stopped", ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    }
                    else if (peer.render_state == "rendering")
                    {
                        drawStatusBadge("Rendering", ImVec4(0.3f, 0.5f, 1.0f, 1.0f));
                    }
                    else
                    {
                        drawStatusBadge("Idle", ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
                    }

                    ImGui::SameLine();
                    if (!peer.hostname.empty())
                        ImGui::TextUnformatted(peer.hostname.c_str());
                    else
                        ImGui::TextUnformatted(peer.node_id.c_str());
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Node ID: %s", peer.node_id.c_str());
                        ImGui::EndTooltip();
                    }

                    // Leader badge
                    if (peer.is_leader)
                    {
                        ImGui::SameLine();
                        drawStatusBadge("Leader", ImVec4(1.0f, 0.84f, 0.0f, 1.0f));
                    }

                    // UDP badge
                    if (peer.has_udp_contact)
                    {
                        ImGui::SameLine();
                        drawStatusBadge("UDP", ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                    }

                    // Suspect badge (suspended by failure tracker)
                    if (m_app->isLeader())
                    {
                        auto& tracker = m_app->dispatchManager().failureTracker();
                        if (tracker.isSuspended(peer.node_id))
                        {
                            ImGui::SameLine();
                            drawStatusBadge("Suspect", ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
                            if (ImGui::IsItemHovered())
                            {
                                auto* record = tracker.getRecord(peer.node_id);
                                if (record)
                                {
                                    ImGui::BeginTooltip();
                                    ImGui::Text("%d failures — not receiving new work", record->failure_count);
                                    ImGui::EndTooltip();
                                }
                            }
                        }
                    }

                    // Agent health badge
                    if (peer.is_alive && peer.agent_health == "needs_attention")
                    {
                        ImGui::SameLine();
                        drawStatusBadge("Agent Down", ImVec4(1.0f, 0.4f, 0.2f, 1.0f));
                        if (ImGui::IsItemHovered() && !peer.alert_reason.empty())
                        {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(peer.alert_reason.c_str());
                            ImGui::EndTooltip();
                        }
                    }

                    // Active job info (if rendering)
                    if (peer.is_alive && peer.render_state == "rendering" && !peer.active_job.empty())
                    {
                        ImGui::TextDisabled("  %s %s", peer.active_job.c_str(), peer.active_chunk.c_str());
                    }

                    // Hardware summary
                    if (peer.is_alive && !peer.hostname.empty())
                    {
                        std::string hw;
                        if (!peer.app_version.empty())
                            hw += "v" + peer.app_version;
                        if (!peer.os.empty())
                        {
                            if (!hw.empty()) hw += " | ";
                            hw += peer.os;
                        }
                        if (peer.cpu_cores > 0)
                        {
                            if (!hw.empty()) hw += " | ";
                            hw += std::to_string(peer.cpu_cores) + " cores";
                        }
                        if (peer.ram_mb > 0)
                        {
                            if (!hw.empty()) hw += " | ";
                            hw += std::to_string(peer.ram_mb / 1024) + " GB";
                        }
                        if (!hw.empty())
                            ImGui::TextDisabled("  %s", hw.c_str());
                    }

                    // Remote control buttons
                    if (peer.is_alive)
                    {
                        ImGui::Indent(16.0f);
                        PushOutlineButtonStyle();
                        if (peer.node_state != "stopped")
                        {
                            if (ImGui::SmallButton("Stop"))
                            {
                                m_app->peerManager().setPeerNodeState(peer.node_id, "stopped");
                                std::string ep = peer.endpoint;
                                std::string secret = m_app->farmSecret();
                                std::thread([ep, secret]() {
                                    auto [host, port] = parseEndpoint(ep);
                                    if (host.empty()) return;
                                    httplib::Client cli(host, port);
                                    cli.set_connection_timeout(2);
                                    cli.set_read_timeout(2);
                                    cli.Post("/api/node/stop", authHeaders(secret), "", "text/plain");
                                }).detach();
                            }
                        }
                        else
                        {
                            if (ImGui::SmallButton("Start"))
                            {
                                m_app->peerManager().setPeerNodeState(peer.node_id, "active");
                                std::string ep = peer.endpoint;
                                std::string secret = m_app->farmSecret();
                                std::thread([ep, secret]() {
                                    auto [host, port] = parseEndpoint(ep);
                                    if (host.empty()) return;
                                    httplib::Client cli(host, port);
                                    cli.set_connection_timeout(2);
                                    cli.set_read_timeout(2);
                                    cli.Post("/api/node/start", authHeaders(secret), "", "text/plain");
                                }).detach();
                            }
                        }

                        // Restart App button
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Restart App"))
                        {
                            std::string ep = peer.endpoint;
                            std::string secret = m_app->farmSecret();
                            std::thread([ep, secret]() {
                                auto [host, port] = parseEndpoint(ep);
                                if (host.empty()) return;
                                httplib::Client cli(host, port);
                                cli.set_connection_timeout(2);
                                cli.set_read_timeout(10);
                                cli.Post("/api/node/restart", authHeaders(secret), "", "text/plain");
                            }).detach();
                        }

                        // Unsuspend button (only leader can unsuspend)
                        if (m_app->isLeader() &&
                            m_app->dispatchManager().failureTracker().isSuspended(peer.node_id))
                        {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Unsuspend"))
                                m_app->unsuspendNode(peer.node_id);
                        }

                        PopOutlineButtonStyle();
                        ImGui::Unindent(16.0f);
                    }

                    // Filesystem restart button for dead peers
                    if (!peer.is_alive)
                    {
                        auto nodeDir = m_app->farmPath() / "nodes" / peer.node_id;
                        std::error_code ec;
                        if (std::filesystem::is_directory(nodeDir, ec))
                        {
                            ImGui::Indent(16.0f);
                            PushOutlineButtonStyle();
                            if (ImGui::SmallButton("Restart (FS)"))
                            {
                                auto signalPath = nodeDir / "restart";
                                std::ofstream ofs(signalPath);
                                // empty file — presence is the signal
                            }
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::TextUnformatted("Write restart signal to shared filesystem (node must still be running)");
                                ImGui::EndTooltip();
                            }
                            PopOutlineButtonStyle();
                            ImGui::Unindent(16.0f);
                        }
                    }

                    ImGui::Dummy(ImVec2(0, 6.0f));  // bottom padding inside shape
                    ImGui::Unindent(8.0f);  // match horizontal padding
                    ImGui::EndGroup();

                    ImVec2 group_end = ImGui::GetItemRectMax();
                    float right_ext = 8.0f;
                    draw_list->AddRect(
                        group_start,
                        ImVec2(group_start.x + (group_end.x - group_start.x) + right_ext,
                               group_end.y),
                        IM_COL32(255, 255, 255, 15),  // subtle white border
                        1.0f, 0, 1.0f);

                    // Dead peers get a dark wash overlay
                    if (!peer.is_alive)
                    {
                        draw_list->AddRectFilled(
                            group_start,
                            ImVec2(group_start.x + (group_end.x - group_start.x) + right_ext,
                                   group_end.y),
                            IM_COL32(30, 30, 30, 180),
                            1.0f);
                    }

                    ImGui::Spacing();
                    ImGui::PopID();
                }
            }
        }
    }
    ImGui::End();
}

} // namespace MR
