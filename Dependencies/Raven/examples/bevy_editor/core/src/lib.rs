//! The heart of the Raven Bevy Editor, with no dependency: what is edited and how.
//! The interface (`editor_ui`) and the renderer (Bevy, or the software one) are built on it.

pub mod camera;
pub mod gizmo;
pub mod history;
pub mod io;
pub mod math;
pub mod pick;
pub mod scene;

pub use camera::{OrbitCamera, Ray};
pub use gizmo::{Gizmo, GizmoMode, Line};
pub use history::History;
pub use math::{Mat4, Vec3};
pub use scene::{Entity, EntityId, Kind, Scene, Shape, Transform};
