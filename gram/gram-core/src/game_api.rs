use crate::{HostAPI, system};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GameAPI {
    pub version: u32,
    pub on_load: extern "C" fn(host: *const HostAPI) -> *const GameAPI,
    pub on_unload: extern "C" fn(),
    pub run_system: extern "C" fn(index: u32, ctx: *mut system::Context)
}

pub const GAME_API_VERSION: u32 = 2;
