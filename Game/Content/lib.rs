mod scripts;

use std::ptr::null;
use gram_core::HostAPI;

use crate::scripts::greeter;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GameAPI {
    pub version: u32,
    pub on_load: extern "C" fn(host: *const HostAPI) -> *const GameAPI,
    pub on_unload: extern "C" fn(),
    pub greet: extern "C" fn(greeting: *mut greeter::Greeting)
}

extern "C" fn on_unload() {
    gram_core::guard(|| { });
}

extern "C" fn on_load(host: *const HostAPI) -> *const GameAPI {
    if !gram_core::init(host) { return null() }

    gram_core::guard(|| {
        // TODO: insert game initialization routine here
        game_get_api()
    }).unwrap_or(null())
}

static GAME: GameAPI = GameAPI {
    version: 1,
    on_load,
    on_unload,
    greet: greeter::greet
};

#[unsafe(no_mangle)]
pub extern "C" fn game_get_api() -> *const GameAPI {
    &GAME as *const GameAPI
}