// Embeds an icon and a full VERSIONINFO resource into mr-agent.exe on
// Windows builds. File/product version come from Cargo.toml's `version`.
//
// The winresource dep only exists for windows targets (see Cargo.toml), so
// the whole body must be cfg'd out on other hosts or `cargo build` on macOS
// fails to resolve the crate. The inner CARGO_CFG_TARGET_OS check covers the
// host-vs-target distinction for cross builds.
#[cfg(windows)]
fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
        let mut res = winresource::WindowsResource::new();
        res.set_icon("../resources/icons/minrender.ico");
        res.set("CompanyName", "cbkow");
        res.set("FileDescription", "MinRender Render Agent");
        res.set("ProductName", "MinRender");
        res.set("LegalCopyright", "Copyright (C) 2025-2026 cbkow");
        res.set("OriginalFilename", "mr-agent.exe");
        res.set("InternalName", "mr-agent");
        res.compile().expect("failed to embed Windows version resource");
        println!("cargo:rerun-if-changed=../resources/icons/minrender.ico");
    }
}

#[cfg(not(windows))]
fn main() {}
