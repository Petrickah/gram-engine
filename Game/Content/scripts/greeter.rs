use gram_core::component;
use gram_core::system;

#[repr(C)]
pub struct Greeting {
    pub text: [u8; 64],
}

impl component::Component for Greeting {
    const ID: component::ComponentID = component::component_id("Greeting");
}

const _: () = assert!(std::mem::size_of::<Greeting>() == 64);

pub(crate) fn greet1(ctx: &system::Context) {
    let Some(entity) = ctx.event_target() else { return };
    // ctx.with_mut(entity, |greeting: &mut Greeting| greeting.text = system::fixed("Hello world from Rust"));
    ctx.with(entity, |greeting: &Greeting| {
        gram_core::info!("Greetings from Rust: {}!", system::text(&greeting.text));
    });
}

pub(crate) fn greet2(_: &system::Context) {
    //let Some(greeting) = (unsafe { greeting. }) else { return };
    gram_core::info!("Greetings from Rust, but from another system!");
    //let message = system::fixed::<64>("Hello world from Rust");
    //greeting.text.copy_from_slice(&message[..]);
}