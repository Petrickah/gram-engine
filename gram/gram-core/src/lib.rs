pub mod system;
pub mod game_api;

use std::ffi::{CString, c_char, c_void};
use std::panic::UnwindSafe;
use std::sync::OnceLock;

#[repr(u32)]
pub enum LogLevel
{
    Unknown,
    Trace,
    Info,
    Warning,
    Error
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct HostAPI {
    pub host: *mut c_void,
    pub log: extern "C" fn(level: LogLevel, message: *const c_char),
    pub register_system: extern "C" fn(host: *mut c_void, desc: *const system::Descriptor)
}

// void* is a generic pointer to an object from C++, Rust just passes it by to the register_system;
// declaring the object static is not safe while holding onto the generic pointer from C++.
unsafe impl Send for HostAPI {}
unsafe impl Sync for HostAPI {}

static HOST: OnceLock<HostAPI> = OnceLock::new();

fn host() -> Option<&'static HostAPI> { HOST.get() }

pub fn init(host: *const HostAPI) -> bool {
    let Some(host) = (unsafe { host.as_ref() }) else { return false; };
    std::panic::set_hook(Box::new(move |ph| {
        let msg = ph.payload().downcast_ref::<&'static str>().copied()
        .or_else(|| ph.payload().downcast_ref::<String>().map(String::as_str))
        .unwrap_or("unknown panic");
    
        let location = match ph.location().cloned() {
            Some(location) => format!("{}:{}:{}", location.file(), location.line(), location.column()),
            None => "unknown file".to_string(),
        };
        
        error!("{location}: {msg}");
    }));

    if HOST.set(*host).is_err() {
        warn!("on_load called twice on the same library");
        return false
    }
    true
}

pub fn guard<R>(f: impl FnOnce() -> R + UnwindSafe) -> Option<R> {
    std::panic::catch_unwind(f).ok()
}

pub fn log(level: LogLevel, msg: &str) {
    let Some(host) = host() else { return };
    let msg = CString::new(msg).unwrap_or_else(|_| c"log message contained a \\0".to_owned());
    (host.log)(level, msg.as_ptr());
}

#[macro_export]
macro_rules! info {
    ($($arg:tt)*) => { $crate::log($crate::LogLevel::Info, &format!($($arg)*)) };
}

#[macro_export]
macro_rules! trace {
    ($($arg:tt)*) => { $crate::log($crate::LogLevel::Trace, &format!($($arg)*)) };
}

#[macro_export]
macro_rules! warn {
    ($($arg:tt)*) => { $crate::log($crate::LogLevel::Warning, &format!($($arg)*)) };
}

#[macro_export]
macro_rules! error {
    ($($arg:tt)*) => { $crate::log($crate::LogLevel::Error, &format!($($arg)*)) };
}
