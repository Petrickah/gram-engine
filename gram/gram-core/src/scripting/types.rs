#[repr(transparent)]
#[derive(Debug)]
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