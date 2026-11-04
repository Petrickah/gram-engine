use crate::{interop::host::HostAPI, scripting::system};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GameAPI {
    pub version: u32,
    pub on_load: extern "C" fn(host: *const HostAPI) -> *const GameAPI,
    pub on_unload: extern "C" fn(),
    pub run_system: extern "C" fn(index: u32, ctx: *mut system::Context)
}

pub const GAME_API_VERSION: u32 = 3;

/// Exports a game library: generates the `GameAPI` table, `on_load` / `on_unload` /
/// `run_system` (each inside `guard`) and `game_get_api`, from the list of systems.
///
/// ```ignore
/// export_game! {
///     events: { "greet" => [greeter::greet1, greeter::greet2] },
///     update: [tick],
/// }
/// ```
#[macro_export]
macro_rules! export_game {
    (
        $( events: { $( $event:literal => [ $( $handler:path ),* $(,)? ] ),* $(,)? } $(,)? )?
        update: [ $( $sys:path ),* $(,)? ] $(,)?
    ) => {
        // Everything generated lives in its own module, so its names can't clash with the game's.
        mod __gram_export {
            #[allow(unused_imports)]
            use super::*;   // the systems named in the macro call, as seen from lib.rs

            const SYSTEMS: &[$crate::scripting::system::Entry] = &[
                $( $crate::scripting::system::Entry {
                    stage: $crate::scripting::system::Stage::Update,
                    name: stringify!($sys),
                    event: "",
                    run: |ctx| $crate::scripting::system::IntoSystem::run(&$sys, ctx),
                }, )*
                // events: optional ($( )?), one entry per event ($( ),*), one per handler ($( ),*):
                // three levels, so $handler is expanded three levels deep and $event two.
                $( $( $( $crate::scripting::system::Entry {
                    stage: $crate::scripting::system::Stage::Event,
                    name: stringify!($handler),
                    event: $event,
                    run: |ctx| $crate::scripting::system::IntoSystem::run(&$handler, ctx),
                }, )* )* )?
            ];

            static GAME: $crate::interop::game::GameAPI = $crate::interop::game::GameAPI {
                version: $crate::interop::game::GAME_API_VERSION,
                on_load,
                on_unload,
                run_system,
            };

            extern "C" fn on_load(host: *const $crate::interop::host::HostAPI) -> *const $crate::interop::game::GameAPI {
                if !$crate::interop::host::init(host) { return ::core::ptr::null() }

                $crate::interop::host::guard(|| {
                    $crate::scripting::system::register(SYSTEMS);
                    &GAME as *const $crate::interop::game::GameAPI
                }).unwrap_or(::core::ptr::null())
            }

            extern "C" fn on_unload() {
                $crate::interop::host::guard(|| {});
            }

            extern "C" fn run_system(index: u32, ctx: *mut $crate::scripting::system::Context) {
                $crate::interop::host::guard(|| {
                    let Some(entry) = SYSTEMS.get(index as usize) else { return };
                    let Some(ctx) = (unsafe { ctx.as_ref() }) else { return };
                    (entry.run)(ctx);
                });
            }

            #[unsafe(no_mangle)]
            pub extern "C" fn game_get_api() -> *const $crate::interop::game::GameAPI {
                &GAME as *const $crate::interop::game::GameAPI
            }
        }
    };
}
