#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct ObjectID {
    index: u32,
    generation: u32,
}

const UINT32_MAX: u32 = 4294967295u32;
pub const INVALID: ObjectID = ObjectID { index: UINT32_MAX, generation: UINT32_MAX };

const _: () = assert!(std::mem::size_of::<ObjectID>() == 8);

impl ObjectID {
    /// Only C++ hands out real IDs; this is for tests.
    #[cfg(test)]
    pub(crate) const fn new(index: u32, generation: u32) -> Self {
        ObjectID { index, generation }
    }

    pub fn is_valid(self) -> bool {
        self != INVALID
    }
}