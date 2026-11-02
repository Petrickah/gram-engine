use std::ptr::null;

use gram_core::HostAPI;
use gram_core::game_api::GameAPI;
use gram_core::system;

use crate::scripts::greeter;

mod scripts;

fn tick(ctx: &system::Context) {
    let (now, before) = (ctx.time(), ctx.time() - ctx.delta_time() as f64);
    if now.floor() != before.floor() {
        gram_core::info!("tick: {}s", now as u64);
    }
}

const SYSTEMS: &[system::Entry] = &[
    system::Entry { stage: system::Stage::Update, name: "tick",   event: "",      run: tick },
    system::Entry { stage: system::Stage::Event,  name: "greet1", event: "greet", run: greeter::greet1 },
    system::Entry { stage: system::Stage::Event,  name: "greet2", event: "greet", run: greeter::greet2 }
];

extern "C" fn on_unload() {
    gram_core::guard(|| { });
}

extern "C" fn on_load(host: *const HostAPI) -> *const GameAPI {
    if !gram_core::init(host) { return null() }

    gram_core::guard(|| {
        system::register(SYSTEMS);
        &GAME as *const GameAPI
    }).unwrap_or(null())
}

extern "C" fn run_system(index: u32, ctx: *mut system::Context) {
    gram_core::guard(std::panic::AssertUnwindSafe(|| {
        let Some(entry) = SYSTEMS.get(index as usize) else { return };
        let Some(ctx) = (unsafe { ctx.as_ref() }) else { return };
        (entry.run)(ctx);
    }));
}

static GAME: GameAPI = GameAPI {
    version: gram_core::game_api::GAME_API_VERSION,
    on_load,
    on_unload,
    run_system
};

#[unsafe(no_mangle)]
pub extern "C" fn game_get_api() -> *const GameAPI {
    &GAME as *const GameAPI
}