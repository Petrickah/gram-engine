#[path = "components.generated.rs"]
pub mod components;
pub mod scripts;

mod prelude {
    pub use gram_core::prelude::*;
    pub use gram_core::scripting::component::*;
    pub use crate::components::*;
}
