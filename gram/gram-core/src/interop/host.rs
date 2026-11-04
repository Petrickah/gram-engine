use crate::interop::logging;
use crate::scripting::system;
use crate::scripting::world::ObjectID;
use crate::{error, warn};

use std::ffi::{c_char, c_void};
use std::panic::UnwindSafe;
use std::sync::OnceLock;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct HostAPI {
    pub host: *mut c_void,
    pub log: extern "C" fn(level: logging::LogLevel, message: *const c_char),
    pub register_system: extern "C" fn(host: *mut c_void, desc: *const system::Descriptor),
    pub get_component: extern "C" fn(host: *mut c_void, entity: ObjectID, component: u64) -> *mut c_void
}

// void* is a generic pointer to an object from C++, Rust just passes it by to the register_system;
// declaring the object static is not safe while holding onto the generic pointer from C++.
unsafe impl Send for HostAPI {}
unsafe impl Sync for HostAPI {}

static HOST: OnceLock<HostAPI> = OnceLock::new();

pub fn host() -> Option<&'static HostAPI> { HOST.get() }

/// The tests' stand-in for C++: stores `api` without `init`'s panic hook (which would replace the
/// hooks the borrow tests install). Once per test binary, like a real load.
#[cfg(test)]
pub(crate) fn install_test_host(api: HostAPI) {
    let _ = HOST.get_or_init(|| api);
}

pub fn init(api: *const HostAPI) -> bool {
    let Some(api) = (unsafe { api.as_ref() }) else { return false; };
    std::panic::set_hook(Box::new(move |ph| {
        let msg = ph.payload().downcast_ref::<&'static str>().copied()
        .or_else(|| ph.payload().downcast_ref::<String>().map(String::as_str))
        .unwrap_or("unknown panic");
    
        let location = match ph.location().cloned() {
            Some(location) => format!("{}:{}:{}", location.file(), location.line(), location.column()),
            None => "unknown file".to_string(),
        };
        
        let line = format!("{location}: {msg}");
        if host().is_some() { error!("{line}"); } else { eprintln!("{line}"); }
    }));

    if HOST.set(*api).is_err() {
        warn!("on_load called twice on the same library");
        return false
    }
    true
}

pub fn guard<R>(f: impl FnOnce() -> R + UnwindSafe) -> Option<R> {
    std::panic::catch_unwind(f).ok()
}
