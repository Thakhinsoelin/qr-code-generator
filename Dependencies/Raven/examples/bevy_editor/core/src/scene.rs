//! The scene being edited: entities in a hierarchy, each with a transform and what it is.

use crate::math::{Mat4, Vec3};

pub type EntityId = u64;

#[derive(Clone, Debug, PartialEq)]
pub struct Transform {
    pub position: Vec3,
    /// Degrees, applied as Bevy's `EulerRot::YXZ`.
    pub rotation: Vec3,
    pub scale: Vec3,
}

impl Default for Transform {
    fn default() -> Self {
        Transform { position: Vec3::ZERO, rotation: Vec3::ZERO, scale: Vec3::ONE }
    }
}

impl Transform {
    pub fn at(position: Vec3) -> Transform {
        Transform { position, ..Default::default() }
    }
    pub fn matrix(&self) -> Mat4 {
        Mat4::from_trs(self.position, self.rotation, self.scale)
    }
}

/// The meshes the editor makes; their sizes are those of the renderer (see `local_bounds`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Shape {
    Cube,
    Sphere,
    Plane,
    Cylinder,
    Capsule,
    Torus,
}

impl Shape {
    pub const ALL: [Shape; 6] = [Shape::Cube, Shape::Sphere, Shape::Plane, Shape::Cylinder, Shape::Capsule, Shape::Torus];

    pub fn name(self) -> &'static str {
        match self {
            Shape::Cube => "cube",
            Shape::Sphere => "sphere",
            Shape::Plane => "plane",
            Shape::Cylinder => "cylinder",
            Shape::Capsule => "capsule",
            Shape::Torus => "torus",
        }
    }

    pub fn from_name(name: &str) -> Option<Shape> {
        Shape::ALL.iter().copied().find(|s| s.name().eq_ignore_ascii_case(name))
    }

    pub fn title(self) -> &'static str {
        match self {
            Shape::Cube => "Cube",
            Shape::Sphere => "Sphere",
            Shape::Plane => "Plane",
            Shape::Cylinder => "Cylinder",
            Shape::Capsule => "Capsule",
            Shape::Torus => "Torus",
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Kind {
    /// A point that only groups others.
    Empty,
    Mesh { shape: Shape, color: [f32; 4], metallic: f32, roughness: f32 },
    PointLight { color: [f32; 3], intensity: f32, range: f32, shadows: bool },
    DirectionalLight { color: [f32; 3], illuminance: f32, shadows: bool },
    /// A camera of the game (the editor looks through its own).
    Camera { fov: f32 },
}

impl Kind {
    pub fn mesh(shape: Shape) -> Kind {
        Kind::Mesh { shape, color: [0.8, 0.8, 0.82, 1.0], metallic: 0.0, roughness: 0.5 }
    }
    pub fn point_light() -> Kind {
        Kind::PointLight { color: [1.0, 0.95, 0.85], intensity: 1_000_000.0, range: 20.0, shadows: true }
    }
    pub fn directional_light() -> Kind {
        Kind::DirectionalLight { color: [1.0, 0.98, 0.92], illuminance: 10_000.0, shadows: true }
    }
    pub fn camera() -> Kind {
        Kind::Camera { fov: 45.0 }
    }
    /// What the hierarchy calls it.
    pub fn title(&self) -> &'static str {
        match self {
            Kind::Empty => "Empty",
            Kind::Mesh { shape, .. } => shape.title(),
            Kind::PointLight { .. } => "Point light",
            Kind::DirectionalLight { .. } => "Directional light",
            Kind::Camera { .. } => "Camera",
        }
    }

    /// A box around what the entity is, in its own space (what picking hits).
    pub fn local_bounds(&self) -> (Vec3, Vec3) {
        match self {
            Kind::Mesh { shape, .. } => match shape {
                Shape::Cube => (Vec3::splat(-0.5), Vec3::splat(0.5)),
                Shape::Sphere => (Vec3::splat(-0.5), Vec3::splat(0.5)),
                Shape::Plane => (Vec3::new(-0.5, -0.01, -0.5), Vec3::new(0.5, 0.01, 0.5)),
                Shape::Cylinder => (Vec3::new(-0.5, -0.5, -0.5), Vec3::new(0.5, 0.5, 0.5)),
                Shape::Capsule => (Vec3::new(-0.5, -1.0, -0.5), Vec3::new(0.5, 1.0, 0.5)),
                Shape::Torus => (Vec3::new(-0.5, -0.125, -0.5), Vec3::new(0.5, 0.125, 0.5)),
            },
            Kind::Empty => (Vec3::splat(-0.15), Vec3::splat(0.15)),
            _ => (Vec3::splat(-0.25), Vec3::splat(0.25)),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Entity {
    pub id: EntityId,
    pub name: String,
    pub parent: Option<EntityId>,
    pub transform: Transform,
    pub visible: bool,
    pub kind: Kind,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Scene {
    /// In the order of the hierarchy: a parent comes before its children, siblings in order.
    pub entities: Vec<Entity>,
    pub next_id: EntityId,
    pub ambient: [f32; 3],
    pub ambient_brightness: f32,
    pub background: [f32; 3],
}

impl Default for Scene {
    fn default() -> Self {
        Scene { entities: Vec::new(), next_id: 1, ambient: [1.0, 1.0, 1.0], ambient_brightness: 300.0, background: [0.11, 0.12, 0.14] }
    }
}

impl Scene {
    /// What a new scene holds: a ground, a cube, a light and a camera.
    pub fn starter() -> Scene {
        let mut scene = Scene::default();
        let ground = scene.add("Ground", Kind::mesh(Shape::Plane), None);
        scene.get_mut(ground).unwrap().transform.scale = Vec3::new(10.0, 1.0, 10.0);
        if let Some(Kind::Mesh { color, roughness, .. }) = scene.get_mut(ground).map(|e| &mut e.kind) {
            *color = [0.35, 0.38, 0.42, 1.0];
            *roughness = 0.9;
        }
        let cube = scene.add("Cube", Kind::mesh(Shape::Cube), None);
        scene.get_mut(cube).unwrap().transform.position = Vec3::new(0.0, 0.5, 0.0);
        if let Some(Kind::Mesh { color, .. }) = scene.get_mut(cube).map(|e| &mut e.kind) {
            *color = [0.29, 0.49, 1.0, 1.0];
        }
        let sun = scene.add("Sun", Kind::directional_light(), None);
        scene.get_mut(sun).unwrap().transform = Transform { position: Vec3::new(-3.0, 5.0, 2.0), rotation: Vec3::new(-50.0, 30.0, 0.0), scale: Vec3::ONE };
        let camera = scene.add("Camera", Kind::camera(), None);
        scene.get_mut(camera).unwrap().transform = Transform { position: Vec3::new(0.0, 3.0, 8.0), rotation: Vec3::new(-20.0, 0.0, 0.0), scale: Vec3::ONE };
        scene
    }

    pub fn index_of(&self, id: EntityId) -> Option<usize> {
        self.entities.iter().position(|e| e.id == id)
    }
    pub fn get(&self, id: EntityId) -> Option<&Entity> {
        self.entities.iter().find(|e| e.id == id)
    }
    pub fn get_mut(&mut self, id: EntityId) -> Option<&mut Entity> {
        self.entities.iter_mut().find(|e| e.id == id)
    }
    pub fn contains(&self, id: EntityId) -> bool {
        self.get(id).is_some()
    }

    /// The children of an entity (None: the roots), in order.
    pub fn children(&self, parent: Option<EntityId>) -> Vec<EntityId> {
        self.entities.iter().filter(|e| e.parent == parent).map(|e| e.id).collect()
    }

    /// The entity, its children, theirs...
    pub fn subtree(&self, id: EntityId) -> Vec<EntityId> {
        let mut out = vec![id];
        let mut i = 0;
        while i < out.len() {
            let current = out[i];
            out.extend(self.children(Some(current)));
            i += 1;
        }
        out
    }

    /// True when `ancestor` is `id` or above it.
    pub fn is_ancestor(&self, ancestor: EntityId, id: EntityId) -> bool {
        let mut walk = Some(id);
        while let Some(current) = walk {
            if current == ancestor {
                return true;
            }
            walk = self.get(current).and_then(|e| e.parent);
        }
        false
    }

    /// A name that no other entity has: "Cube", "Cube 2"...
    pub fn unique_name(&self, base: &str) -> String {
        if !self.entities.iter().any(|e| e.name == base) {
            return base.to_string();
        }
        let mut n = 2;
        loop {
            let candidate = format!("{} {}", base, n);
            if !self.entities.iter().any(|e| e.name == candidate) {
                return candidate;
            }
            n += 1;
        }
    }

    /// Adds an entity at the end of the children of `parent`.
    pub fn add(&mut self, name: &str, kind: Kind, parent: Option<EntityId>) -> EntityId {
        let parent = parent.filter(|p| self.contains(*p));
        let id = self.next_id;
        self.next_id += 1;
        let entity = Entity { id, name: self.unique_name(name), parent, transform: Transform::default(), visible: true, kind };
        let at = self.insertion_index(parent, usize::MAX);
        self.entities.insert(at, entity);
        id
    }

    /// Where an entity goes in `entities` to be the `position`-th child of `parent` (usize::MAX: the last).
    fn insertion_index(&self, parent: Option<EntityId>, position: usize) -> usize {
        let siblings = self.children(parent);
        if position < siblings.len() {
            return self.index_of(siblings[position]).unwrap_or(self.entities.len());
        }
        // after the last sibling and everything under it, or right after the parent
        match siblings.last() {
            Some(&last) => {
                let subtree = self.subtree(last);
                subtree.iter().filter_map(|id| self.index_of(*id)).max().map(|i| i + 1).unwrap_or(self.entities.len())
            }
            None => match parent {
                Some(p) => self.index_of(p).map(|i| i + 1).unwrap_or(self.entities.len()),
                None => self.entities.len(),
            },
        }
    }

    /// Removes an entity and everything under it. Returns how many were removed.
    pub fn remove(&mut self, id: EntityId) -> usize {
        let doomed = self.subtree(id);
        let before = self.entities.len();
        self.entities.retain(|e| !doomed.contains(&e.id));
        before - self.entities.len()
    }

    /// A copy of an entity and of what is under it, right after it. Returns the id of the copy.
    pub fn duplicate(&mut self, id: EntityId) -> Option<EntityId> {
        let original = self.get(id)?.clone();
        let tree = self.subtree(id);
        let mut map: Vec<(EntityId, EntityId)> = Vec::new();
        let mut copies: Vec<Entity> = Vec::new();
        for old in &tree {
            let mut copy = self.get(*old)?.clone();
            let new_id = self.next_id;
            self.next_id += 1;
            map.push((*old, new_id));
            copy.id = new_id;
            if *old == id {
                copy.name = self.unique_name(&strip_number(&original.name));
            } else {
                copy.parent = copy.parent.and_then(|p| map.iter().find(|(o, _)| *o == p).map(|(_, n)| *n));
            }
            copies.push(copy);
        }
        // after the original and what is under it
        let at = tree.iter().filter_map(|t| self.index_of(*t)).max().map(|i| i + 1).unwrap_or(self.entities.len());
        let new_root = copies[0].id;
        for (k, copy) in copies.into_iter().enumerate() {
            self.entities.insert(at + k, copy);
        }
        Some(new_root)
    }

    /// Moves an entity (and what is under it) under another, at a position among its children.
    /// Refused when the new parent is the entity or below it.
    pub fn set_parent(&mut self, id: EntityId, parent: Option<EntityId>, position: usize) -> bool {
        if let Some(p) = parent {
            if !self.contains(p) || self.is_ancestor(id, p) {
                return false;
            }
        }
        if !self.contains(id) {
            return false;
        }
        let tree = self.subtree(id);
        let mut moved: Vec<Entity> = Vec::new();
        for t in &tree {
            if let Some(i) = self.index_of(*t) {
                moved.push(self.entities.remove(i));
            }
        }
        moved[0].parent = parent;
        let at = self.insertion_index(parent, position);
        for (k, e) in moved.into_iter().enumerate() {
            self.entities.insert(at + k, e);
        }
        true
    }

    /// Up (-1) or down (+1) among its siblings.
    pub fn move_among_siblings(&mut self, id: EntityId, step: i32) -> bool {
        let parent = match self.get(id) {
            Some(e) => e.parent,
            None => return false,
        };
        let siblings = self.children(parent);
        let index = siblings.iter().position(|s| *s == id).unwrap_or(0) as i32;
        let target = index + step;
        if target < 0 || target >= siblings.len() as i32 {
            return false;
        }
        // (moving down by one: after the next sibling, which is position target once removed)
        self.set_parent(id, parent, target as usize)
    }

    /// Into the sibling before it (the last of its children).
    pub fn indent(&mut self, id: EntityId) -> bool {
        let parent = match self.get(id) {
            Some(e) => e.parent,
            None => return false,
        };
        let siblings = self.children(parent);
        let index = siblings.iter().position(|s| *s == id).unwrap_or(0);
        if index == 0 {
            return false;
        }
        let keep = self.world_matrix(id);
        let ok = self.set_parent(id, Some(siblings[index - 1]), usize::MAX);
        if ok {
            self.keep_world(id, keep);
        }
        ok
    }

    /// Out of its parent, right after it.
    pub fn outdent(&mut self, id: EntityId) -> bool {
        let parent = match self.get(id).and_then(|e| e.parent) {
            Some(p) => p,
            None => return false,
        };
        let grand = self.get(parent).and_then(|e| e.parent);
        let position = self.children(grand).iter().position(|s| *s == parent).map(|p| p + 1).unwrap_or(usize::MAX);
        let keep = self.world_matrix(id);
        let ok = self.set_parent(id, grand, position);
        if ok {
            self.keep_world(id, keep);
        }
        ok
    }

    /// Moves an entity under another parent (the last of its children), where it is in the world staying the same.
    pub fn reparent(&mut self, id: EntityId, parent: Option<EntityId>) -> bool {
        if self.get(id).map(|e| e.parent) == Some(parent) {
            return false;
        }
        let keep = self.world_matrix(id);
        let ok = self.set_parent(id, parent, usize::MAX);
        if ok {
            self.keep_world(id, keep);
        }
        ok
    }

    /// Changes the local transform of an entity so that its world matrix stays `world` (after a change of parent).
    fn keep_world(&mut self, id: EntityId, world: Mat4) {
        let parent = self.get(id).and_then(|e| e.parent);
        let parent_world = parent.map(|p| self.world_matrix(p)).unwrap_or(Mat4::IDENTITY);
        if let Some(inverse) = parent_world.inverse() {
            let local = inverse * world;
            if let Some(e) = self.get_mut(id) {
                e.transform = decompose(&local, &e.transform);
            }
        }
    }

    pub fn world_matrix(&self, id: EntityId) -> Mat4 {
        let mut chain = Vec::new();
        let mut walk = Some(id);
        while let Some(current) = walk {
            match self.get(current) {
                Some(e) => {
                    chain.push(e.transform.matrix());
                    walk = e.parent;
                }
                None => break,
            }
        }
        chain.into_iter().rev().fold(Mat4::IDENTITY, |acc, m| acc * m)
    }

    /// Visible itself and all its parents.
    pub fn is_shown(&self, id: EntityId) -> bool {
        let mut walk = Some(id);
        while let Some(current) = walk {
            match self.get(current) {
                Some(e) if !e.visible => return false,
                Some(e) => walk = e.parent,
                None => return false,
            }
        }
        true
    }

    /// How deep in the hierarchy (0 for a root).
    pub fn depth(&self, id: EntityId) -> usize {
        let mut depth = 0;
        let mut walk = self.get(id).and_then(|e| e.parent);
        while let Some(current) = walk {
            depth += 1;
            walk = self.get(current).and_then(|e| e.parent);
        }
        depth
    }
}

/// "Cube 3" -> "Cube"
fn strip_number(name: &str) -> String {
    match name.rsplit_once(' ') {
        Some((base, tail)) if !base.is_empty() && tail.chars().all(|c| c.is_ascii_digit()) => base.to_string(),
        _ => name.to_string(),
    }
}

/// Position, Euler angles (YXZ, degrees) and scale of a matrix without shear. `hint` keeps the
/// signs of the scale when they cannot be told apart.
pub fn decompose(m: &Mat4, hint: &Transform) -> Transform {
    let position = m.origin();
    let cx = Vec3::new(m.get(0, 0), m.get(1, 0), m.get(2, 0));
    let cy = Vec3::new(m.get(0, 1), m.get(1, 1), m.get(2, 1));
    let cz = Vec3::new(m.get(0, 2), m.get(1, 2), m.get(2, 2));
    let mut scale = Vec3::new(cx.length(), cy.length(), cz.length());
    if hint.scale.x < 0.0 {
        scale.x = -scale.x;
    }
    if hint.scale.y < 0.0 {
        scale.y = -scale.y;
    }
    if hint.scale.z < 0.0 {
        scale.z = -scale.z;
    }
    let rx = if scale.x.abs() > 1e-9 { cx * (1.0 / scale.x) } else { Vec3::X };
    let ry = if scale.y.abs() > 1e-9 { cy * (1.0 / scale.y) } else { Vec3::Y };
    let rz = if scale.z.abs() > 1e-9 { cz * (1.0 / scale.z) } else { Vec3::Z };
    // R = Ry(a) Rx(b) Rz(c): r12 = -sin(b), r02 = sin(a)cos(b), r22 = cos(a)cos(b), r10 = cos(b)sin(c), r11 = cos(b)cos(c)
    let r12 = rz.y;
    let b = (-r12).clamp(-1.0, 1.0).asin();
    let (a, c) = if b.cos().abs() > 1e-6 {
        (rz.x.atan2(rz.z), rx.y.atan2(ry.y))
    } else {
        // looking straight up or down: the roll goes into the yaw
        ((-rx.z).atan2(rx.x), 0.0)
    };
    Transform { position, rotation: Vec3::new(b.to_degrees(), a.to_degrees(), c.to_degrees()), scale }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hierarchy() {
        let mut s = Scene::default();
        let a = s.add("A", Kind::Empty, None);
        let b = s.add("B", Kind::mesh(Shape::Cube), Some(a));
        let c = s.add("C", Kind::mesh(Shape::Sphere), Some(b));
        let d = s.add("D", Kind::Empty, None);
        assert_eq!(s.children(None), vec![a, d]);
        assert_eq!(s.subtree(a), vec![a, b, c]);
        assert!(s.is_ancestor(a, c));
        // a parent never goes under its own child
        assert!(!s.set_parent(a, Some(c), 0));
        assert!(s.set_parent(b, Some(d), 0));
        assert_eq!(s.children(Some(d)), vec![b]);
        // the order of the list follows the hierarchy
        let order: Vec<_> = s.entities.iter().map(|e| e.id).collect();
        assert_eq!(order, vec![a, d, b, c]);
        assert_eq!(s.remove(d), 3);
        assert_eq!(s.entities.len(), 1);
    }

    #[test]
    fn duplicate_keeps_the_children() {
        let mut s = Scene::default();
        let a = s.add("Cube", Kind::mesh(Shape::Cube), None);
        let _b = s.add("Child", Kind::Empty, Some(a));
        let copy = s.duplicate(a).unwrap();
        assert_eq!(s.get(copy).unwrap().name, "Cube 2");
        assert_eq!(s.children(Some(copy)).len(), 1);
        assert_eq!(s.children(None), vec![a, copy]);
    }

    #[test]
    fn siblings_and_indent() {
        let mut s = Scene::default();
        let a = s.add("A", Kind::Empty, None);
        let b = s.add("B", Kind::Empty, None);
        let c = s.add("C", Kind::Empty, None);
        assert!(s.move_among_siblings(c, -1));
        assert_eq!(s.children(None), vec![a, c, b]);
        assert!(s.move_among_siblings(a, 1));
        assert_eq!(s.children(None), vec![c, a, b]);
        s.get_mut(a).unwrap().transform.position = Vec3::new(1.0, 0.0, 0.0);
        s.get_mut(b).unwrap().transform.position = Vec3::new(3.0, 0.0, 0.0);
        // into A: it stays where it was in the world
        assert!(s.indent(b));
        assert_eq!(s.get(b).unwrap().parent, Some(a));
        assert!((s.get(b).unwrap().transform.position.x - 2.0).abs() < 1e-4);
        assert!(s.outdent(b));
        assert_eq!(s.get(b).unwrap().parent, None);
        assert!((s.get(b).unwrap().transform.position.x - 3.0).abs() < 1e-4);
    }

    #[test]
    fn decompose_round_trip() {
        let t = Transform { position: Vec3::new(1.0, -2.0, 3.0), rotation: Vec3::new(20.0, -35.0, 50.0), scale: Vec3::new(1.5, 2.0, 0.5) };
        let back = decompose(&t.matrix(), &t);
        assert!((back.position - t.position).length() < 1e-4);
        assert!((back.rotation - t.rotation).length() < 1e-2, "{:?}", back.rotation);
        assert!((back.scale - t.scale).length() < 1e-4);
    }
}
