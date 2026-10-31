use std::ffi::c_char;
use std::sync::OnceLock;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct HostAPI {
    pub log: extern "C" fn(message: *const c_char),
}

static HOST: OnceLock<HostAPI> = OnceLock::new();

#[repr(C)]
pub struct Greeting {
    pub text: [u8; 64],
}

const _: () = assert!(std::mem::size_of::<Greeting>() == 64);

#[unsafe(no_mangle)]
pub extern "C" fn gram_core_init(host: *const HostAPI) -> bool {
    let Some(host) = (unsafe {host.as_ref()}) else { return false };
    HOST.set(*host).is_ok()
}

#[unsafe(no_mangle)]
pub extern "C" fn gram_core_greet(greeting: *mut Greeting) {
    let Some(greeting) = (unsafe {greeting.as_mut()}) else { return };
    let message = b"Hello world from Rust";

    let len = message.len().min(greeting.text.len() - 1); // păstrează loc pentru \0
    greeting.text.fill(0);
    greeting.text[..len].copy_from_slice(&message[..len]);

    if let Some(host) = HOST.get() {
        (host.log)(c"Greeting written from Rust".as_ptr());
    }
}
