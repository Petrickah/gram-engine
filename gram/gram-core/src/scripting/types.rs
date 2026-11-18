#[repr(transparent)]
#[derive(Debug)]
// Str3 struct equivalent for C standard strings;
// Has transparent layout, with 1 byte per character and alignment to 1 byte
pub struct Str<const N: usize>([u8; N]);

impl<const N: usize> Str<N> {
    pub fn new(text: &str) -> Self {
        let mut s = Self([0; N]);
        s.set(text);
        s
    }

    pub fn set(&mut self, text: &str) {
        self.0 = [0u8; N];
        let len = text.len().min(N.saturating_sub(1));
        self.0[..len].copy_from_slice(&text.as_bytes()[..len]);
    }

    /// The inverse of `set`: the text in a `\0`-terminated fixed buffer, up to the first `\0`
    /// (the whole buffer if there's none). Stops before the first invalid UTF-8 byte instead of
    /// failing, since C++ (an ImGui text field) may have written anything there.
    pub fn as_str(&self) -> &str {
        let len = self.0.iter().position(|&b| b == 0).unwrap_or(self.0.len());
        match std::str::from_utf8(&self.0[..len]) {
            Ok(text) => text,
            Err(error) => std::str::from_utf8(&self.0[..error.valid_up_to()]).unwrap_or_default(),
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq)]
/// Vec3 struct equivalent for C++ glm::vec3;
/// Has layout standard C, with 12 bytes (3 floats) and alignment to 4 bytes.
pub struct Vec3 { pub x: f32, pub y: f32, pub z: f32 }

const _ : () = assert!(std::mem::size_of::<Vec3>() == 12);

impl Vec3 {
    pub const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }
}