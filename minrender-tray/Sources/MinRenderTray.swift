import SwiftUI
import Foundation

/// Tray state read from the sidecar's IPC socket.
struct TrayState: Codable {
    var tray_icon: String       // "green", "blue", "yellow", "red", "gray"
    var tray_tooltip: String
    var tray_status: String
    var node_state: String      // "active" or "stopped"

    static let empty = TrayState(
        tray_icon: "gray", tray_tooltip: "MinRender", tray_status: "Not Connected", node_state: "active"
    )
}

@main
struct MinRenderTrayApp: App {
    @StateObject private var model = TrayModel()

    var body: some Scene {
        MenuBarExtra {
            VStack(alignment: .leading, spacing: 4) {
                Text("MinRender")
                    .font(.headline)

                Divider()

                Text("Status: \(model.state.tray_status)")
                    .foregroundColor(.secondary)
                    .font(.caption)

                Divider()

                Button("Open MinRender") {
                    model.openTauriApp()
                }

                Button(model.state.node_state == "active" ? "Stop Node" : "Resume Node") {
                    model.toggleNodeState()
                }

                Divider()

                Button("Quit") {
                    NSApplication.shared.terminate(nil)
                }
            }
            .padding(4)
        } label: {
            Image(systemName: model.iconName)
                .symbolRenderingMode(.hierarchical)
        }
    }
}

@MainActor
class TrayModel: ObservableObject {
    @Published var state = TrayState.empty
    @Published var connected = false

    private var pollTimer: Timer?
    private let socketPath: String

    var iconName: String {
        switch state.tray_icon {
        case "green": return "desktopcomputer"
        case "blue": return "desktopcomputer"
        case "yellow": return "exclamationmark.triangle"
        case "red": return "xmark.circle"
        default: return "desktopcomputer"
        }
    }

    init() {
        // Find the node ID to construct the socket path
        let nodeId = Self.readNodeId() ?? "unknown"
        self.socketPath = "/tmp/minrender-agent-ui_\(nodeId).sock"

        // Poll the sidecar for tray state every 3 seconds
        pollTimer = Timer.scheduledTimer(withTimeInterval: 3.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                self?.pollSidecar()
            }
        }
        pollTimer?.fire()
    }

    func pollSidecar() {
        // Connect to the Unix socket, send get_state, read response
        // For simplicity, use HTTP API instead of raw IPC (sidecar runs on localhost:8420)
        guard let url = URL(string: "http://127.0.0.1:8420/api/status") else { return }

        URLSession.shared.dataTask(with: url) { [weak self] data, response, error in
            DispatchQueue.main.async {
                guard let self = self else { return }
                guard let data = data, error == nil else {
                    self.connected = false
                    self.state = TrayState.empty
                    return
                }
                self.connected = true

                // Parse status for tray-relevant fields
                if let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                    let renderState = json["render_state"] as? String ?? "idle"
                    let nodeState = json["node_state"] as? String ?? "active"
                    let hostname = json["hostname"] as? String ?? ""

                    var icon = "blue"
                    var tooltip = "MinRender"
                    var status = "Idle"

                    if nodeState == "stopped" {
                        icon = "gray"; status = "Stopped"
                    } else if renderState == "rendering" {
                        icon = "green"
                        let jobId = json["active_job"] as? String ?? ""
                        status = "Rendering \(jobId)"
                    }

                    if !hostname.isEmpty {
                        tooltip = "MinRender — \(hostname)"
                    }

                    self.state = TrayState(
                        tray_icon: icon,
                        tray_tooltip: tooltip,
                        tray_status: status,
                        node_state: nodeState
                    )
                }
            }
        }.resume()
    }

    func openTauriApp() {
        // Try to launch the Tauri app
        let candidates = [
            "/Applications/MinRender Monitor.app",
            NSHomeDirectory() + "/Applications/MinRender Monitor.app",
        ]
        for path in candidates {
            if FileManager.default.fileExists(atPath: path) {
                NSWorkspace.shared.open(URL(fileURLWithPath: path))
                return
            }
        }
        NSLog("[MinRenderTray] Could not find MinRender Monitor app")
    }

    func toggleNodeState() {
        let endpoint = state.node_state == "active" ? "/api/node/stop" : "/api/node/start"
        guard let url = URL(string: "http://127.0.0.1:8420\(endpoint)") else { return }
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        URLSession.shared.dataTask(with: request) { _, _, _ in
            DispatchQueue.main.async { self.pollSidecar() }
        }.resume()
    }

    static func readNodeId() -> String? {
        let configDir = NSHomeDirectory() + "/Library/Application Support/MinRender"
        let nodeIdPath = configDir + "/node_id"
        return try? String(contentsOfFile: nodeIdPath, encoding: .utf8).trimmingCharacters(in: .whitespacesAndNewlines)
    }
}
