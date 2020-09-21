#[cfg(windows)]
fn main() {
    let res = winres::WindowsResource::new();
    res.compile().unwrap();
}

#[cfg(not(windows))]
fn main() {
    println!("cargo:rerun-if-changed=build.rs");
}
