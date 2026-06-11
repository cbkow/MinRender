use std::process::{Child, Command};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;

/// Kills the entire process tree of a render child, immediately.
///
/// Created at spawn time and shared via Arc between the executor (abort),
/// the timeout watchdog thread, and the worker thread (which marks the
/// child reaped and reads back the kill reason). All methods take &self,
/// so no thread needs &mut Child.
///
/// Unix: the child is spawned into its own process group (prepare_command),
/// so SIGKILL to -pgid takes out every descendant.
/// Windows: the child is assigned to a Job Object with KILL_ON_JOB_CLOSE,
/// so TerminateJobObject takes out every descendant — and the tree also
/// dies if the agent itself crashes (the OS closes the job handle).
pub struct ProcessKiller {
    pid: u32,
    killed: AtomicBool,
    finished: AtomicBool,
    kill_reason: Mutex<Option<String>>,
    #[cfg(windows)]
    job: Option<JobHandle>,
}

#[cfg(windows)]
struct JobHandle(windows::Win32::Foundation::HANDLE);

// Job-object handles are thread-safe kernel objects; HANDLE is !Send/!Sync
// only because it wraps a raw pointer.
#[cfg(windows)]
unsafe impl Send for JobHandle {}
#[cfg(windows)]
unsafe impl Sync for JobHandle {}

#[cfg(windows)]
impl Drop for JobHandle {
    fn drop(&mut self) {
        // KILL_ON_JOB_CLOSE: closing the last handle terminates any
        // processes still in the job. Harmless after a normal exit.
        unsafe {
            let _ = windows::Win32::Foundation::CloseHandle(self.0);
        }
    }
}

impl ProcessKiller {
    /// Platform setup that must happen BEFORE spawn.
    #[cfg(unix)]
    pub fn prepare_command(cmd: &mut Command) {
        use std::os::unix::process::CommandExt;
        // setpgid(0, 0) between fork and exec — the group exists before
        // the child runs any code, so there is no assignment race.
        cmd.process_group(0);
    }

    #[cfg(windows)]
    pub fn prepare_command(_cmd: &mut Command) {}

    /// Platform setup AFTER spawn. Infallible by design: if job-object
    /// setup fails on Windows, kill() falls back to `taskkill /T /F`.
    pub fn new(child: &Child) -> Self {
        Self {
            pid: child.id(),
            killed: AtomicBool::new(false),
            finished: AtomicBool::new(false),
            kill_reason: Mutex::new(None),
            #[cfg(windows)]
            job: create_job_for(child),
        }
    }

    /// Kill the whole process tree immediately. Idempotent — the first
    /// caller wins and records `reason`; a no-op once the child has been
    /// reaped (guards against killing a recycled pgid on Unix).
    pub fn kill(&self, reason: &str) {
        if self.finished.load(Ordering::SeqCst) {
            return;
        }
        if self.killed.swap(true, Ordering::SeqCst) {
            return;
        }
        if let Ok(mut r) = self.kill_reason.lock() {
            *r = Some(reason.to_string());
        }
        log::info!("Killing render process tree (pid={}): {}", self.pid, reason);
        self.kill_tree();
    }

    /// Worker calls this immediately after child.wait() returns.
    pub fn mark_finished(&self) {
        self.finished.store(true, Ordering::SeqCst);
    }

    pub fn is_finished(&self) -> bool {
        self.finished.load(Ordering::SeqCst)
    }

    /// Why the tree was killed, if it was.
    pub fn kill_reason(&self) -> Option<String> {
        self.kill_reason.lock().ok().and_then(|r| r.clone())
    }

    #[cfg(unix)]
    fn kill_tree(&self) {
        unsafe {
            libc::kill(-(self.pid as libc::pid_t), libc::SIGKILL);
        }
    }

    #[cfg(windows)]
    fn kill_tree(&self) {
        use windows::Win32::System::JobObjects::TerminateJobObject;
        match &self.job {
            Some(j) => unsafe {
                let _ = TerminateJobObject(j.0, 1);
            },
            None => {
                let _ = Command::new("taskkill")
                    .args(["/PID", &self.pid.to_string(), "/T", "/F"])
                    .stdout(std::process::Stdio::null())
                    .stderr(std::process::Stdio::null())
                    .status();
            }
        }
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;
    use std::time::{Duration, Instant};

    fn group_alive(pgid: u32) -> bool {
        // Signal 0 = existence check for the process group.
        unsafe { libc::kill(-(pgid as libc::pid_t), 0) == 0 }
    }

    #[test]
    fn kill_takes_out_grandchildren_of_silent_process() {
        // Parent prints nothing, spawns a backgrounded grandchild, sleeps.
        let mut cmd = Command::new("sh");
        cmd.args(["-c", "sleep 300 & sleep 300"]);
        ProcessKiller::prepare_command(&mut cmd);
        let mut child = cmd.spawn().expect("spawn");
        let killer = ProcessKiller::new(&child);

        std::thread::sleep(Duration::from_millis(200));
        assert!(group_alive(killer.pid));

        killer.kill("test abort");
        let _ = child.wait();
        killer.mark_finished();

        // The grandchild (backgrounded sleep) must die with the group.
        let deadline = Instant::now() + Duration::from_secs(2);
        while group_alive(killer.pid) && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(50));
        }
        assert!(!group_alive(killer.pid), "process group survived kill");
        assert_eq!(killer.kill_reason().as_deref(), Some("test abort"));
    }

    #[test]
    fn kill_is_noop_after_finish() {
        let mut cmd = Command::new("true");
        ProcessKiller::prepare_command(&mut cmd);
        let mut child = cmd.spawn().expect("spawn");
        let killer = ProcessKiller::new(&child);
        let _ = child.wait();
        killer.mark_finished();

        killer.kill("too late");
        assert_eq!(killer.kill_reason(), None);
    }
}

#[cfg(windows)]
fn create_job_for(child: &Child) -> Option<JobHandle> {
    use std::os::windows::io::AsRawHandle;
    use windows::core::PCWSTR;
    use windows::Win32::Foundation::{CloseHandle, HANDLE};
    use windows::Win32::System::JobObjects::{
        AssignProcessToJobObject, CreateJobObjectW, JobObjectExtendedLimitInformation,
        SetInformationJobObject, JOBOBJECT_EXTENDED_LIMIT_INFORMATION,
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
    };

    unsafe {
        let job = match CreateJobObjectW(None, PCWSTR::null()) {
            Ok(j) => j,
            Err(e) => {
                log::warn!("CreateJobObjectW failed ({}); will use taskkill fallback", e);
                return None;
            }
        };

        let mut info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION::default();
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

        let result = SetInformationJobObject(
            job,
            JobObjectExtendedLimitInformation,
            &info as *const _ as *const _,
            std::mem::size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32,
        )
        .and_then(|_| AssignProcessToJobObject(job, HANDLE(child.as_raw_handle() as _)));

        match result {
            Ok(_) => Some(JobHandle(job)),
            Err(e) => {
                log::warn!("Job object setup failed ({}); will use taskkill fallback", e);
                let _ = CloseHandle(job);
                None
            }
        }
    }
}
