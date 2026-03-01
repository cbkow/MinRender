mod executor;
mod ipc;
mod messages;
mod parser;
mod watchdog;

use clap::Parser;
use executor::{RenderEvent, RenderExecutor};
use messages::{
    AckMessage, AgentToMonitor, CompletedMessage, FailedMessage, FrameCompletedMessage,
    MonitorToAgent, ProgressMessage, StatusMessage, StdoutMessage,
};
use std::process;
use std::thread;
use std::time::Duration;

#[derive(Parser)]
#[command(name = "mr-agent", about = "MinRender headless render agent", version)]
struct Args {
    /// Node ID to connect to (must match the monitor's node ID)
    #[arg(long)]
    node_id: String,
}

/// Holds the named mutex handle to keep it alive for the process lifetime.
#[cfg(windows)]
struct MutexGuard {
    _handle: windows::Win32::Foundation::HANDLE,
}

#[cfg(windows)]
impl Drop for MutexGuard {
    fn drop(&mut self) {
        if !self._handle.is_invalid() {
            unsafe { let _ = windows::Win32::Foundation::CloseHandle(self._handle); }
        }
    }
}

#[cfg(not(windows))]
struct MutexGuard;

/// Creates a named mutex to prevent duplicate agents for the same node.
/// Exits with error if another agent is already running.
#[cfg(windows)]
fn ensure_single_instance(node_id: &str) -> MutexGuard {
    use windows::core::PCWSTR;
    use windows::Win32::Foundation::GetLastError;
    use windows::Win32::Foundation::ERROR_ALREADY_EXISTS;
    use windows::Win32::System::Threading::CreateMutexW;

    let mutex_name: Vec<u16> = format!("MinRenderAgent_{}\0", node_id)
        .encode_utf16()
        .collect();

    let handle = unsafe { CreateMutexW(None, false, PCWSTR(mutex_name.as_ptr())) };
    match handle {
        Ok(h) => {
            if unsafe { GetLastError() } == ERROR_ALREADY_EXISTS {
                log::error!("Another mr-agent is already running for node_id={}", node_id);
                process::exit(1);
            }
            MutexGuard { _handle: h }
        }
        Err(e) => {
            log::error!("Failed to create instance mutex: {}", e);
            process::exit(1);
        }
    }
}

#[cfg(not(windows))]
fn ensure_single_instance(_node_id: &str) -> MutexGuard {
    MutexGuard
}

/// Resolve the log file path: %LOCALAPPDATA%/MinRender/mr-agent.log (Windows)
/// or ~/.local/share/MinRender/mr-agent.log (other).
fn log_file_path() -> Option<std::path::PathBuf> {
    #[cfg(windows)]
    {
        if let Ok(local) = std::env::var("LOCALAPPDATA") {
            let dir = std::path::PathBuf::from(local).join("MinRender");
            let _ = std::fs::create_dir_all(&dir);
            return Some(dir.join("mr-agent.log"));
        }
    }
    #[cfg(not(windows))]
    {
        if let Some(home) = std::env::var_os("HOME") {
            let dir = std::path::PathBuf::from(home)
                .join(".local/share/MinRender");
            let _ = std::fs::create_dir_all(&dir);
            return Some(dir.join("mr-agent.log"));
        }
    }
    None
}

/// Initialize logging: write to both stderr and a log file.
fn init_logging() {
    use simplelog::*;

    let level = LevelFilter::Info;
    let config = ConfigBuilder::new()
        .set_time_format_rfc3339()
        .build();

    let mut loggers: Vec<Box<dyn SharedLogger>> = vec![
        TermLogger::new(level, config.clone(), TerminalMode::Stderr, ColorChoice::Auto),
    ];

    if let Some(path) = log_file_path() {
        // Truncate if log is > 2 MB to avoid unbounded growth
        if let Ok(meta) = std::fs::metadata(&path) {
            if meta.len() > 2 * 1024 * 1024 {
                let _ = std::fs::remove_file(&path);
            }
        }
        match std::fs::OpenOptions::new().create(true).append(true).open(&path) {
            Ok(file) => {
                loggers.push(WriteLogger::new(level, config.clone(), file));
                eprintln!("[mr-agent] Logging to {}", path.display());
            }
            Err(e) => {
                eprintln!("[mr-agent] Warning: could not open log file {}: {}", path.display(), e);
            }
        }
    }

    CombinedLogger::init(loggers).unwrap_or_else(|e| {
        eprintln!("[mr-agent] Failed to init logger: {}", e);
    });
}

/// Install a panic hook that logs the panic info to our log file before aborting.
fn install_panic_hook() {
    let default_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        let location = info.location().map(|l| format!("{}:{}:{}", l.file(), l.line(), l.column()))
            .unwrap_or_else(|| "unknown".to_string());
        let payload = if let Some(s) = info.payload().downcast_ref::<&str>() {
            s.to_string()
        } else if let Some(s) = info.payload().downcast_ref::<String>() {
            s.clone()
        } else {
            "unknown panic payload".to_string()
        };
        log::error!("PANIC at {}: {}", location, payload);

        // Also write directly to the log file in case the logger is broken
        if let Some(path) = log_file_path() {
            let msg = format!(
                "[PANIC] {} at {}\n",
                payload, location
            );
            let _ = std::fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(&path)
                .and_then(|mut f| {
                    use std::io::Write;
                    f.write_all(msg.as_bytes())
                });
        }

        default_hook(info);
    }));
}

/// Attempt to reconnect to the monitor pipe after a transient disconnect.
/// Tries 6 times with 5-second intervals (~30 seconds total, matching monitor grace period).
fn attempt_reconnect(node_id: &str) -> Option<ipc::PipeClient> {
    for attempt in 1..=6 {
        log::info!("Reconnect attempt {}/6...", attempt);
        thread::sleep(Duration::from_secs(5));
        match ipc::PipeClient::connect(node_id) {
            Ok(p) => return Some(p),
            Err(e) => log::warn!("Reconnect attempt {} failed: {}", attempt, e),
        }
    }
    None
}

fn main() {
    init_logging();
    install_panic_hook();

    let args = Args::parse();
    log::info!("mr-agent v{} starting for node_id={}", env!("CARGO_PKG_VERSION"), args.node_id);

    // Single instance check — prevent duplicate agents for the same node
    let _mutex_guard = ensure_single_instance(&args.node_id);

    let mut pipe = match ipc::PipeClient::connect(&args.node_id) {
        Ok(p) => p,
        Err(e) => {
            log::error!("Failed to connect to monitor: {}", e);
            process::exit(1);
        }
    };

    // Send initial status: idle + our PID
    let status = AgentToMonitor::Status(StatusMessage {
        state: "idle".into(),
        pid: process::id(),
    });
    if let Err(e) = send_message(&mut pipe, &status) {
        log::error!("Failed to send initial status: {}", e);
        process::exit(1);
    }
    log::info!("Sent initial status (pid={})", process::id());

    let mut active_render: Option<RenderExecutor> = None;

    loop {
        if let Some(ref executor) = active_render {
            // === RENDERING MODE ===
            let mut done = false;
            let mut undelivered_done: Option<AgentToMonitor> = None;

            // 1. Process render events (non-blocking)
            for event in executor.poll_events() {
                match event {
                    RenderEvent::Started => {
                        log::info!(
                            "Render started: job={} chunk={}-{}",
                            executor.job_id,
                            executor.frame_start,
                            executor.frame_end,
                        );
                        let _ = send_message(
                            &mut pipe,
                            &AgentToMonitor::Ack(AckMessage {
                                job_id: executor.job_id.clone(),
                                frame_start: executor.frame_start,
                                frame_end: executor.frame_end,
                            }),
                        );
                    }
                    RenderEvent::Stdout(lines) => {
                        let _ = send_message(
                            &mut pipe,
                            &AgentToMonitor::Stdout(StdoutMessage {
                                job_id: executor.job_id.clone(),
                                frame_start: executor.frame_start,
                                frame_end: executor.frame_end,
                                lines,
                            }),
                        );
                    }
                    RenderEvent::Progress { pct, elapsed_ms } => {
                        let _ = send_message(
                            &mut pipe,
                            &AgentToMonitor::Progress(ProgressMessage {
                                job_id: executor.job_id.clone(),
                                frame_start: executor.frame_start,
                                frame_end: executor.frame_end,
                                progress_pct: pct,
                                elapsed_ms,
                            }),
                        );
                    }
                    RenderEvent::FrameCompleted { frame } => {
                        let _ = send_message(
                            &mut pipe,
                            &AgentToMonitor::FrameCompleted(FrameCompletedMessage {
                                job_id: executor.job_id.clone(),
                                frame,
                            }),
                        );
                    }
                    RenderEvent::Completed {
                        elapsed_ms,
                        exit_code,
                        output_file,
                    } => {
                        log::info!(
                            "Render completed: job={} chunk={}-{} exit_code={} elapsed={}ms",
                            executor.job_id,
                            executor.frame_start,
                            executor.frame_end,
                            exit_code,
                            elapsed_ms,
                        );
                        let msg = AgentToMonitor::Completed(CompletedMessage {
                            job_id: executor.job_id.clone(),
                            frame_start: executor.frame_start,
                            frame_end: executor.frame_end,
                            elapsed_ms,
                            exit_code,
                            output_file,
                        });
                        if send_message(&mut pipe, &msg).is_err() {
                            undelivered_done = Some(msg);
                        }
                        done = true;
                    }
                    RenderEvent::Failed { exit_code, error } => {
                        log::warn!(
                            "Render failed: job={} chunk={}-{} exit_code={} error={}",
                            executor.job_id,
                            executor.frame_start,
                            executor.frame_end,
                            exit_code,
                            error,
                        );
                        let msg = AgentToMonitor::Failed(FailedMessage {
                            job_id: executor.job_id.clone(),
                            frame_start: executor.frame_start,
                            frame_end: executor.frame_end,
                            exit_code,
                            error,
                        });
                        if send_message(&mut pipe, &msg).is_err() {
                            undelivered_done = Some(msg);
                        }
                        done = true;
                    }
                }
            }

            if done {
                if let Some(msg) = undelivered_done {
                    // Completion/failure message lost (pipe broken) — reconnect to deliver
                    log::warn!("Pipe broken at render completion — reconnecting to deliver result");
                    drop(pipe);
                    match attempt_reconnect(&args.node_id) {
                        Some(mut new_pipe) => {
                            log::info!("Reconnected — delivering completion message");
                            let _ = send_message(&mut new_pipe, &msg);
                            let _ = send_message(
                                &mut new_pipe,
                                &AgentToMonitor::Status(StatusMessage {
                                    state: "idle".into(),
                                    pid: process::id(),
                                }),
                            );
                        }
                        None => {
                            log::error!("Failed to reconnect — completion message lost");
                        }
                    }
                } else {
                    let _ = send_message(
                        &mut pipe,
                        &AgentToMonitor::Status(StatusMessage {
                            state: "idle".into(),
                            pid: process::id(),
                        }),
                    );
                }
                active_render = None;
                break;
            }

            // 2. Check IPC messages (non-blocking via peek)
            let mut pipe_error: Option<String> = None;

            match pipe.peek_available() {
                Ok(available) if available > 0 => {
                    match ipc::read_message(&mut pipe) {
                        Ok(payload) => {
                            if let Ok(msg) = serde_json::from_slice::<MonitorToAgent>(&payload) {
                                match msg {
                                    MonitorToAgent::Ping => {
                                        let _ = send_message(&mut pipe, &AgentToMonitor::Pong);
                                    }
                                    MonitorToAgent::Shutdown => {
                                        log::info!("Received shutdown during render, aborting");
                                        if let Some(ref exec) = active_render {
                                            exec.abort();
                                        }
                                        // Wait briefly for worker to finish
                                        thread::sleep(Duration::from_millis(500));
                                        break;
                                    }
                                    MonitorToAgent::Abort(abort) => {
                                        log::info!("Received abort: {}", abort.reason);
                                        if let Some(ref exec) = active_render {
                                            exec.abort();
                                        }
                                    }
                                    MonitorToAgent::Task(_) => {
                                        log::warn!("Received task while already rendering, ignoring");
                                    }
                                }
                            }
                        }
                        Err(e) => {
                            pipe_error = Some(format!("Pipe read error during render: {}", e));
                        }
                    }
                }
                Ok(_) => {
                    // No data available, continue polling
                }
                Err(e) => {
                    pipe_error = Some(format!("Pipe peek error during render: {}", e));
                }
            }

            // Pipe error — attempt reconnection instead of aborting
            if let Some(err_msg) = pipe_error {
                log::warn!("{} — attempting reconnect", err_msg);
                drop(pipe);
                match attempt_reconnect(&args.node_id) {
                    Some(new_pipe) => {
                        pipe = new_pipe;
                        log::info!("Reconnected to monitor pipe — continuing render");
                        // Re-announce rendering state so monitor knows we're alive
                        let _ = send_message(
                            &mut pipe,
                            &AgentToMonitor::Status(StatusMessage {
                                state: "rendering".into(),
                                pid: process::id(),
                            }),
                        );
                    }
                    None => {
                        log::error!("Failed to reconnect — aborting render");
                        if let Some(ref exec) = active_render {
                            exec.abort();
                        }
                        break;
                    }
                }
            }

            thread::sleep(Duration::from_millis(100));
        } else {
            // === IDLE MODE === (blocking read)
            let payload = match ipc::read_message(&mut pipe) {
                Ok(data) => data,
                Err(e) => {
                    log::error!("Pipe read error (monitor disconnected?): {}", e);
                    break;
                }
            };

            let msg: MonitorToAgent = match serde_json::from_slice(&payload) {
                Ok(m) => m,
                Err(e) => {
                    log::warn!("Failed to parse message: {}", e);
                    continue;
                }
            };

            match msg {
                MonitorToAgent::Ping => {
                    log::debug!("Received ping, sending pong");
                    if let Err(e) = send_message(&mut pipe, &AgentToMonitor::Pong) {
                        log::error!("Failed to send pong: {}", e);
                        break;
                    }
                }
                MonitorToAgent::Shutdown => {
                    log::info!("Received shutdown command, exiting");
                    break;
                }
                MonitorToAgent::Task(task) => {
                    log::info!(
                        "Received task: job={} chunk={}-{} cmd={}",
                        task.job_id,
                        task.frame_start,
                        task.frame_end,
                        task.command.executable,
                    );
                    // Capture task info before move for error reporting
                    let job_id = task.job_id.clone();
                    let frame_start = task.frame_start;
                    let frame_end = task.frame_end;

                    match RenderExecutor::start(task) {
                        Ok(executor) => {
                            let _ = send_message(
                                &mut pipe,
                                &AgentToMonitor::Status(StatusMessage {
                                    state: "rendering".into(),
                                    pid: process::id(),
                                }),
                            );
                            active_render = Some(executor);
                        }
                        Err(e) => {
                            log::error!("Failed to start render: {}", e);
                            let _ = send_message(
                                &mut pipe,
                                &AgentToMonitor::Failed(FailedMessage {
                                    job_id,
                                    frame_start,
                                    frame_end,
                                    exit_code: -1,
                                    error: format!("Failed to start render: {}", e),
                                }),
                            );
                        }
                    }
                }
                MonitorToAgent::Abort(_) => {
                    // Nothing to abort
                }
            }
        }
    }

    log::info!("mr-agent exiting");
}

fn send_message(
    pipe: &mut ipc::PipeClient,
    msg: &AgentToMonitor,
) -> Result<(), Box<dyn std::error::Error>> {
    let payload = serde_json::to_vec(msg)?;
    ipc::write_message(pipe, &payload)?;
    Ok(())
}
