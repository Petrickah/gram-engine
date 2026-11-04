#[path = "components.generated.rs"]
pub mod components;
pub mod scripts;

mod prelude {
    pub use gram_core::prelude::*;
    pub use crate::components::*;
}
