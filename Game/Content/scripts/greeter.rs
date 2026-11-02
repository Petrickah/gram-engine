#[repr(C)]
pub struct Greeting {
    pub text: [u8; 64],
}

const _: () = assert!(std::mem::size_of::<Greeting>() == 64);

pub(crate) extern "C" fn greet(greeting: *mut Greeting) {
    gram_core::guard(std::panic::AssertUnwindSafe(|| {
        let Some(greeting) = (unsafe { greeting.as_mut() }) else { return };
        let message = b"Hello world from Rust";
        let len = message.len().min(greeting.text.len() - 1);
        gram_core::info!("Greetings from Rust!");
        greeting.text.fill(0);
        greeting.text[..len].copy_from_slice(&message[..len]);
    }));
}