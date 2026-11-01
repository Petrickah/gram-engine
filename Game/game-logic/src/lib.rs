use std::ptr::null;

use gram_core::HostAPI;

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

extern "C" fn on_unload() {
    gram_core::guard(|| { });
}

extern "C" fn on_load(host: *const HostAPI) -> *const GameAPI {
    if !gram_core::init(host) { return null() }

    gram_core::guard(|| {
        // TODO: insert game initialization routine here
        &GAME as *const GameAPI
    }).unwrap_or(null())
}

extern "C" fn greet(greeting: *mut Greeting) {
    gram_core::guard(std::panic::AssertUnwindSafe(|| {
        let Some(greeting) = (unsafe { greeting.as_mut() }) else { return };
        let message = b"Hello world from Rust";
        let len = message.len().min(greeting.text.len() - 1);
        greeting.text.fill(0);
        greeting.text[..len].copy_from_slice(&message[..len]);
        gram_core::info!("Greetings from Rust!");
    }));
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