use std::ffi::c_char;

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
    pub log: extern "C" fn(level: LogLevel, message: *const c_char),
}
