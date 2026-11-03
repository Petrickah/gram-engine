pub mod greeter;

use crate::prelude::*;

fn tick(ctx: &Context) {
    let (now, before) = (ctx.time(), ctx.time() - ctx.delta_time() as f64);
    if now.floor() != before.floor() && before >= 0.0 {
        info!("tick: {}s", now as u64);
    }
}

export_game! {
    events: { "greet" => [greeter::greet1, greeter::greet2] },
    update: [tick],
}