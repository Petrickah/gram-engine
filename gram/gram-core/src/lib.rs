pub mod interop;
pub mod scripting;

/// What a game script usually needs, in one line: `use gram_core::prelude::*;`.
pub mod prelude {
    pub use crate::scripting::system::{Context, DeltaTime, EventTarget, Query, Time};
    pub use crate::scripting::types::Str;
    pub use crate::scripting::world::ObjectID;
    pub use crate::{error, export_game, info, trace, warn};
}

/// What the code `gram generate` emits imports (`use gram_core::codegen::*;`), not meant for
/// scripts. A new schema field type adds its Rust type here, next to its `FieldType` case.
#[doc(hidden)]
pub mod codegen {
    pub use crate::scripting::component::{Component, ComponentID, component_id};
    pub use crate::scripting::types::Str;
}
