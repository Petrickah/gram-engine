use std::ffi::{c_double, c_float};

use crate::host;

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
    time: c_double
}

impl Context {
    pub fn delta_time(&self) -> f32 {
        self.delta_time
    }

    pub fn time(&self) -> f64 {
        self.time
    }
}

const _: () = assert!(std::mem::size_of::<Context>() == 16);

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