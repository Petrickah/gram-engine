use std::ffi::{c_double, c_float};

use crate::{component::Component, host, world::ObjectID};

#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Stage {
    Update = 0,
    Event  = 1
}

const _: () = assert!(std::mem::size_of::<Stage>() == 4);

#[repr(C)]
pub struct Descriptor {
    name: [u8; 64],
    stage: Stage,
    event: [u8; 64],
    index: u32
}

const _: () = assert!(std::mem::size_of::<Descriptor>() == 136);

#[repr(C)]
pub struct Context {
    delta_time: c_float,
    time: c_double,
    event_target: ObjectID
}

impl Context {
    pub fn delta_time(&self) -> f32 {
        self.delta_time
    }

    pub fn time(&self) -> f64 {
        self.time
    }

    pub fn event_target(&self) -> Option<ObjectID> {
        if self.event_target.is_valid() { Some(self.event_target) } else { None }
    }

    /// Lends `entity`'s `T` to `f`, read-only. `None` when the entity is gone or has no `T`.
    /// The reference can't outlive `f`: C++ only guarantees the component's address while a
    /// system runs.
    pub fn with<T: Component, R>(&self, entity: ObjectID, f: impl FnOnce(&T) -> R) -> Option<R> {
        let host = crate::host()?;
        let ptr = (host.get_component)(host.host, entity, T::ID) as *const T;
        let component = unsafe { ptr.as_ref() }?;
        Some(f(component))
    }

    /// Like `with`, but `f` may change the component. Don't nest two `with_mut` on the same
    /// component: that would be two `&mut` to the same memory.
    pub fn with_mut<T: Component, R>(&self, entity: ObjectID, f: impl FnOnce(&mut T) -> R) -> Option<R> {
        let host = crate::host()?;
        let ptr = (host.get_component)(host.host, entity, T::ID) as *mut T;
        let component = unsafe { ptr.as_mut() }?;   // null → None: entitatea a murit sau n-are componenta
        Some(f(component))
    }
}

const _: () = assert!(std::mem::size_of::<Context>() == 24);

pub type System = fn(&Context);

pub struct Entry {
    pub stage: Stage,
    pub name: &'static str,
    pub event: &'static str,
    pub run: System,
}

impl Entry {
    pub fn descriptor(&self, index: u32) -> Descriptor {
        Descriptor { 
            name: fixed::<64>(self.name),
            stage: self.stage,
            event: fixed::<64>(self.event),
            index
        }
    }
}

pub fn register(entries: &[Entry]) {
    let Some(host) = host() else { return };
    entries.iter().enumerate().for_each(|(index, entry)| {
        (host.register_system)(host.host, &entry.descriptor(index as u32))
    });
}

pub fn fixed<const N: usize>(s: &str) -> [u8; N] {
    let mut out = [0u8; N];
    let len = s.len().min(N.saturating_sub(1));
    out[..len].copy_from_slice(&s.as_bytes()[..len]);
    out
}

/// The inverse of `fixed`: the text in a `\0`-terminated fixed buffer, up to the first `\0`
/// (the whole buffer if there's none). Stops before the first invalid UTF-8 byte instead of
/// failing, since C++ (an ImGui text field) may have written anything there.
pub fn text(bytes: &[u8]) -> &str {
    let len = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    match std::str::from_utf8(&bytes[..len]) {
        Ok(text) => text,
        Err(error) => std::str::from_utf8(&bytes[..error.valid_up_to()]).unwrap_or_default(),
    }
}
