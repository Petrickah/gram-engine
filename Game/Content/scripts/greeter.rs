use crate::prelude::*;

pub(crate) fn greet1(target: EventTarget, greetings: Query<Greeting>) {
    let Some(entity) = *target else { return };

    if let Some(greeting) = greetings.get(entity) {
        info!("Greetings from Rust: {}!", &greeting.text.as_str());
    }

    if let Some(mut greeting) = greetings.get_mut(entity) {
        greeting.text.set("Hello world from Rust")
    }
}

pub(crate) fn greet2(_: &Context) {
    //let Some(greeting) = (unsafe { greeting. }) else { return };
    info!("Greetings from Rust, but from another system!");
    //let message = system::fixed::<64>("Hello world from Rust");
    //greeting.text.copy_from_slice(&message[..]);
}
