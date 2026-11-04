pub mod interop;
pub mod scripting;

/// What a game script usually needs, in one line: `use gram_core::prelude::*;`.
pub mod prelude {
    pub use crate::scripting::system::{Context, DeltaTime, EventTarget, Query, Time};
    pub use crate::scripting::types::Str;
    pub use crate::scripting::world::ObjectID;
    pub use crate::{error, export_game, info, trace, warn};
}
