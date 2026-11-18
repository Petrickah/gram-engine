pub type ComponentID = u64;

const _: () = assert!(std::mem::size_of::<ComponentID>() == 8);

pub const fn component_id(name: &str) -> ComponentID { 
    let mut hash: ComponentID = 0xcbf29ce484222325u64;
    let bytes = name.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        hash ^= bytes[i] as u64;
        hash  = hash.wrapping_mul(0x100000001b3u64);
        i += 1;
    }

    hash
}

const _: () = assert!(component_id("Greeting") == 0xe18e41722408a7d6u64);

/// A component's Rust type: plain `#[repr(C)]` data living in C++'s `World`, never holding a reference
/// of its own, hence `'static` (which also lets a `Ref<'a, T>` exist for any context lifetime `'a`).
pub trait Component: 'static { const ID: ComponentID; }