use gram_core::system;

use crate::scripts::greeter;

#[path = "components.generated.rs"]
pub mod components;

mod scripts;

fn tick(ctx: &system::Context) {
    let (now, before) = (ctx.time(), ctx.time() - ctx.delta_time() as f64);
    if now.floor() != before.floor() && before >= 0.0 {
        gram_core::info!("tick: {}s", now as u64);
    }
}

gram_core::export_game! {
    events: { "greet" => [greeter::greet1, greeter::greet2] },
    update: [tick],
}