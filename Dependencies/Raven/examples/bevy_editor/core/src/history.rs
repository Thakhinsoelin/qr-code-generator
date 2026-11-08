//! Undo and redo: a copy of the scene (and of the selection) before each change.
//!
//! Changes that follow each other with the same key (dragging a gizmo, typing in a field)
//! make a single step, so one Ctrl+Z undoes the whole gesture.

use crate::scene::{EntityId, Scene};

#[derive(Clone, Debug)]
struct Step {
    label: String,
    scene: Scene,
    selection: Option<EntityId>,
}

#[derive(Debug, Default)]
pub struct History {
    undo: Vec<Step>,
    redo: Vec<Step>,
    /// The key of the last change, while it can still be merged with the next one.
    open: Option<String>,
    limit: usize,
    /// Changes since the scene was last saved (negative after undoing past it).
    dirty: i64,
}

impl History {
    pub fn new() -> History {
        History { limit: 200, ..Default::default() }
    }

    /// To call before changing the scene, with what it is about to be changed from.
    /// `merge_key`: a change with the same key as the one just before joins it.
    pub fn record(&mut self, label: &str, before: &Scene, selection: Option<EntityId>, merge_key: Option<&str>) {
        if let (Some(key), Some(open)) = (merge_key, self.open.as_deref()) {
            if key == open {
                return;
            }
        }
        self.undo.push(Step { label: label.to_string(), scene: before.clone(), selection });
        if self.undo.len() > self.limit {
            self.undo.remove(0);
        }
        self.redo.clear();
        self.open = merge_key.map(|k| k.to_string());
        self.dirty += 1;
    }

    /// Ends the merging of changes (the mouse was released, the field lost focus...).
    pub fn close(&mut self) {
        self.open = None;
    }

    pub fn can_undo(&self) -> bool {
        !self.undo.is_empty()
    }
    pub fn can_redo(&self) -> bool {
        !self.redo.is_empty()
    }
    pub fn undo_label(&self) -> Option<&str> {
        self.undo.last().map(|s| s.label.as_str())
    }
    pub fn redo_label(&self) -> Option<&str> {
        self.redo.last().map(|s| s.label.as_str())
    }

    /// Puts back the scene before the last change; `scene` and `selection` are the current ones.
    pub fn undo(&mut self, scene: &mut Scene, selection: &mut Option<EntityId>) -> Option<String> {
        let step = self.undo.pop()?;
        self.open = None;
        self.redo.push(Step { label: step.label.clone(), scene: std::mem::replace(scene, step.scene), selection: *selection });
        *selection = step.selection.filter(|id| scene.contains(*id));
        self.dirty -= 1;
        Some(step.label)
    }

    pub fn redo(&mut self, scene: &mut Scene, selection: &mut Option<EntityId>) -> Option<String> {
        let step = self.redo.pop()?;
        self.open = None;
        self.undo.push(Step { label: step.label.clone(), scene: std::mem::replace(scene, step.scene), selection: *selection });
        *selection = step.selection.filter(|id| scene.contains(*id));
        self.dirty += 1;
        Some(step.label)
    }

    /// Drops the last step without undoing it (a gesture that was cancelled and put back by hand).
    pub fn forget_last(&mut self) {
        if self.undo.pop().is_some() {
            self.open = None;
            self.dirty -= 1;
        }
    }

    pub fn clear(&mut self) {
        self.undo.clear();
        self.redo.clear();
        self.open = None;
        self.dirty = 0;
    }

    pub fn mark_saved(&mut self) {
        self.dirty = 0;
        self.open = None;
    }

    pub fn is_dirty(&self) -> bool {
        self.dirty != 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::math::Vec3;
    use crate::scene::{Kind, Shape};

    #[test]
    fn undo_redo_and_merge() {
        let mut scene = Scene::default();
        let mut selection = None;
        let mut h = History::new();
        h.record("Add cube", &scene, selection, None);
        let cube = scene.add("Cube", Kind::mesh(Shape::Cube), None);
        selection = Some(cube);
        // a drag: three changes, one step
        for x in 1..=3 {
            h.record("Move", &scene, selection, Some("move"));
            scene.get_mut(cube).unwrap().transform.position = Vec3::new(x as f32, 0.0, 0.0);
        }
        h.close();
        assert!(h.is_dirty());
        assert_eq!(h.undo(&mut scene, &mut selection).as_deref(), Some("Move"));
        assert_eq!(scene.get(cube).unwrap().transform.position, Vec3::ZERO);
        assert_eq!(h.undo(&mut scene, &mut selection).as_deref(), Some("Add cube"));
        assert!(scene.entities.is_empty());
        assert_eq!(selection, None);
        assert!(!h.is_dirty());
        h.redo(&mut scene, &mut selection);
        h.redo(&mut scene, &mut selection);
        assert_eq!(scene.get(cube).unwrap().transform.position.x, 3.0);
        assert_eq!(selection, Some(cube));
        assert!(!h.can_redo());
    }
}
