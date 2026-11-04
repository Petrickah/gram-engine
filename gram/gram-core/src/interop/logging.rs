use std::ffi::CString;

use crate::interop::host::host;

#[repr(u32)]
pub enum LogLevel
{
    Unknown,
    Trace,
    Info,
    Warning,
    Error
}

pub fn log(level: LogLevel, msg: &str) {
    let Some(host) = host() else { return };
    let msg = CString::new(msg).unwrap_or_else(|_| c"log message contained a \\0".to_owned());
    (host.log)(level, msg.as_ptr());
}

#[macro_export]
macro_rules! info {
    ($($arg:tt)*) => { $crate::interop::logging::log($crate::interop::logging::LogLevel::Info, &format!($($arg)*)) };
}

#[macro_export]
macro_rules! trace {
    ($($arg:tt)*) => { $crate::interop::logging::log($crate::interop::logging::LogLevel::Trace, &format!($($arg)*)) };
}

#[macro_export]
macro_rules! warn {
    ($($arg:tt)*) => { $crate::interop::logging::log($crate::interop::logging::LogLevel::Warning, &format!($($arg)*)) };
}

#[macro_export]
macro_rules! error {
    ($($arg:tt)*) => { $crate::interop::logging::log($crate::interop::logging::LogLevel::Error, &format!($($arg)*)) };
}