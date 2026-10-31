use std::{ptr::null, sync::OnceLock};

use gram_core::{HostAPI, LogLevel};

#[repr(C)]
pub struct Greeting {
    pub text: [u8; 64],
}

const _: () = assert!(std::mem::size_of::<Greeting>() == 64);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GameAPI {
    pub version: u32,
    pub on_load: extern "C" fn(host: *const HostAPI) -> *const GameAPI,
    pub on_unload: extern "C" fn(),
    pub greet: extern "C" fn(greeting: *mut Greeting)
}

static HOST: OnceLock<HostAPI> = OnceLock::new();

extern "C" fn on_unload() {}
extern "C" fn on_load(host: *const HostAPI) -> *const GameAPI {
    let Some(host) = (unsafe { host.as_ref() }) else { return null(); };
    match HOST.set(*host) {
        Ok(_) => &GAME,
        Err(_) => null(),
    }
}
extern "C" fn greet(greeting: *mut Greeting)
{
    let Some(greeting) = (unsafe {greeting.as_mut()}) else { return };
    let message = b"Hello world from Rust";

    let len = message.len().min(greeting.text.len() - 1); // păstrează loc pentru \0
    greeting.text.fill(0);
    greeting.text[..len].copy_from_slice(&message[..len]);

    if let Some(host) = HOST.get() {
        (host.log)(LogLevel::Info, c"Greetings from Rust!".as_ptr());
    }
}

static GAME: GameAPI = GameAPI {
    version: 1,
    on_load,
    on_unload,
    greet
};

#[unsafe(no_mangle)]
pub extern "C" fn game_get_api() -> *const GameAPI {
    &GAME
}