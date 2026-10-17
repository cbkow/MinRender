use std::io::{self, Read, Write};
use std::os::unix::io::AsRawFd;
use std::os::unix::net::UnixStream;
use std::thread;
use std::time::Duration;

/// Unix domain socket client mirroring `WindowsPipeClient`'s public surface.
/// Connects to the sidecar's listener at `$TMPDIR/minrender-agent-{nodeId}.sock`
/// (falling back to /tmp) — must match agentSocketPath() in
/// src/core/ipc_server.cpp exactly.
pub struct UnixSocketClient {
    stream: UnixStream,
}

/// sun_path capacity on this platform; a path that would not fit falls
/// back to /tmp on both sides.
#[cfg(target_os = "macos")]
const SUN_PATH_LEN: usize = 104;
#[cfg(not(target_os = "macos"))]
const SUN_PATH_LEN: usize = 108;

fn socket_path(node_id: &str) -> String {
    let name = format!("minrender-agent-{}.sock", node_id);
    let mut dir = std::env::var("TMPDIR").unwrap_or_default();
    if dir.is_empty() {
        dir = "/tmp".to_string();
    }
    while dir.len() > 1 && dir.ends_with('/') {
        dir.pop();
    }
    let path = format!("{}/{}", dir, name);
    if path.len() >= SUN_PATH_LEN {
        format!("/tmp/{}", name)
    } else {
        path
    }
}

impl UnixSocketClient {
    /// Connect to the monitor's Unix socket for the given node_id.
    /// Retries up to 3 times with 3-second intervals, mirroring
    /// WindowsPipeClient::connect.
    pub fn connect(node_id: &str) -> io::Result<Self> {
        let socket_path = socket_path(node_id);
        let max_attempts = 3;
        let retry_delay = Duration::from_secs(3);

        for attempt in 1..=max_attempts {
            log::info!(
                "Connecting to socket: {} (attempt {}/{})",
                socket_path, attempt, max_attempts
            );

            match UnixStream::connect(&socket_path) {
                Ok(stream) => {
                    log::info!("Connected to monitor socket");
                    return Ok(Self { stream });
                }
                Err(e) if attempt < max_attempts => {
                    log::warn!("UnixStream::connect failed: {}, retrying in {}s...",
                        e, retry_delay.as_secs());
                    thread::sleep(retry_delay);
                }
                Err(e) => {
                    return Err(io::Error::new(
                        io::ErrorKind::ConnectionRefused,
                        format!("Failed to connect to {} after {} attempts: {}",
                            socket_path, max_attempts, e),
                    ));
                }
            }
        }

        Err(io::Error::new(
            io::ErrorKind::ConnectionRefused,
            format!("Failed to connect to socket {} after {} attempts",
                socket_path, max_attempts),
        ))
    }

    /// Check how many bytes are available to read without blocking.
    /// Returns 0 when the socket is connected but nothing is pending —
    /// matches main.rs's poll-loop contract (peek_available, then
    /// read_message only on > 0).
    ///
    /// `UnixStream::peek` is still unstable on stable Rust as of 1.94,
    /// so we go through libc::recv with MSG_PEEK | MSG_DONTWAIT — same
    /// kernel semantics, no change to the socket's blocking mode (so
    /// the subsequent read_message() blocks normally).
    pub fn peek_available(&self) -> io::Result<usize> {
        let mut buf = [0u8; 1];
        let fd = self.stream.as_raw_fd();
        let n = unsafe {
            libc::recv(
                fd,
                buf.as_mut_ptr() as *mut libc::c_void,
                buf.len(),
                libc::MSG_PEEK | libc::MSG_DONTWAIT,
            )
        };
        if n > 0 {
            return Ok(n as usize);
        }
        if n == 0 {
            // recv returns 0 when the peer has closed the connection.
            return Err(io::Error::new(
                io::ErrorKind::BrokenPipe,
                "socket closed (0-byte peek)",
            ));
        }
        let err = io::Error::last_os_error();
        match err.raw_os_error() {
            Some(libc::EAGAIN) | Some(libc::EWOULDBLOCK) => Ok(0),
            _ => Err(err),
        }
    }
}

impl Read for UnixSocketClient {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if buf.is_empty() {
            return Ok(0);
        }
        let n = self.stream.read(buf)?;
        // Match WindowsPipeClient: 0 bytes from a non-empty read means
        // the peer closed the socket cleanly.
        if n == 0 {
            return Err(io::Error::new(
                io::ErrorKind::BrokenPipe,
                "socket closed (0-byte read)",
            ));
        }
        Ok(n)
    }
}

impl Write for UnixSocketClient {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.stream.write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.stream.flush()
    }
}
