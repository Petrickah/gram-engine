use std::ffi::{c_double, c_float};

use crate::{borrow::BorrowGuard, component::Component, host, types::Str, world::ObjectID};

#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Stage {
    Update = 0,
    Event  = 1
}

const _: () = assert!(std::mem::size_of::<Stage>() == 4);

#[repr(C)]
pub struct Descriptor {
    name: Str<64>,
    stage: Stage,
    event: Str<64>,
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
    #[track_caller]
    pub fn with<T: Component, R>(&self, entity: ObjectID, f: impl FnOnce(&T) -> R) -> Option<R> {
        let host = crate::host()?;
        let ptr = (host.get_component)(host.host, entity, T::ID) as *const T;
        // Before the reference exists: a conflict must panic before there are two of them.
        let _borrow = BorrowGuard::acquire(entity, T::ID, false);
        let component = unsafe { ptr.as_ref() }?;
        Some(f(component))
    }

    /// Like `with`, but `f` may change the component. Don't nest two `with_mut` on the same
    /// component: that would be two `&mut` to the same memory.
    #[track_caller]
    pub fn with_mut<T: Component, R>(&self, entity: ObjectID, f: impl FnOnce(&mut T) -> R) -> Option<R> {
        let host = crate::host()?;
        let ptr = (host.get_component)(host.host, entity, T::ID) as *mut T;
        // Before the reference exists: a conflict must panic before there are two of them.
        let _borrow = BorrowGuard::acquire(entity, T::ID, true);
        let component = unsafe { ptr.as_mut() }?;
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
            name: Str::<64>::new(self.name),
            stage: self.stage,
            event: Str::<64>::new(self.event),
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
