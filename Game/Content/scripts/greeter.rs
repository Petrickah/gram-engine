use crate::prelude::*;

pub(crate) fn greet1(ctx: &Context) {
    let Some(entity) = ctx.event_target() else { return };
    // ctx.with_mut(entity, |greeting: &mut Greeting| greeting.text = system::fixed("Hello world from Rust"));
    ctx.with(entity, |greeting: &Greeting| {
        gram_core::info!("Greetings from Rust: {}!", &greeting.text.as_str());
    });
}

pub(crate) fn greet2(_: &Context) {
    //let Some(greeting) = (unsafe { greeting. }) else { return };
    gram_core::info!("Greetings from Rust, but from another system!");
    //let message = system::fixed::<64>("Hello world from Rust");
    //greeting.text.copy_from_slice(&message[..]);
}