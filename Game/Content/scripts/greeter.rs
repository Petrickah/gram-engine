use gram_core::system;

#[repr(C)]
pub struct Greeting {
    pub text: [u8; 64],
}

const _: () = assert!(std::mem::size_of::<Greeting>() == 64);

pub(crate) fn greet1(_: &system::Context) {
    //let Some(greeting) = (unsafe { greeting. }) else { return };
    gram_core::info!("Greetings from Rust!");
    //let message = system::fixed::<64>("Hello world from Rust");
    //greeting.text.copy_from_slice(&message[..]);
}

pub(crate) fn greet2(_: &system::Context) {
    //let Some(greeting) = (unsafe { greeting. }) else { return };
    gram_core::info!("Greetings from Rust, but from another system!");
    //let message = system::fixed::<64>("Hello world from Rust");
    //greeting.text.copy_from_slice(&message[..]);
}