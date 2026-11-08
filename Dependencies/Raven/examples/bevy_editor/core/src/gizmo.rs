//! The handles drawn on the selected entity: arrows to move it, rings to turn it, boxes to scale it.
//!
//! Handles 0, 1, 2 are the axes X, Y, Z; handle 3 is the middle (move in the plane of the view,
//! or scale the same on every axis).

use crate::camera::{OrbitCamera, Ray};
use crate::math::{Mat4, Vec3};
use crate::scene::{decompose, EntityId, Scene, Transform};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GizmoMode {
    Move,
    Rotate,
    Scale,
}

pub const AXIS_COLORS: [[f32; 4]; 4] = [[0.93, 0.27, 0.3, 1.0], [0.45, 0.82, 0.27, 1.0], [0.27, 0.53, 0.98, 1.0], [0.92, 0.92, 0.92, 1.0]];
pub const HOVER_COLOR: [f32; 4] = [1.0, 0.85, 0.2, 1.0];

/// A segment to draw, in the world.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Line {
    pub a: Vec3,
    pub b: Vec3,
    pub color: [f32; 4],
}

#[derive(Clone, Debug)]
struct Drag {
    handle: usize,
    id: EntityId,
    start: Transform,
    start_world: Mat4,
    parent_inverse: Mat4,
    center: Vec3,
    axis: Vec3,
    /// Move and scale: where on the axis the mouse was taken. Rotate: the direction from the middle.
    start_param: f32,
    start_vector: Vec3,
    start_pixel: (f32, f32),
}

#[derive(Clone, Debug)]
pub struct Gizmo {
    pub mode: GizmoMode,
    pub hovered: Option<usize>,
    drag: Option<Drag>,
    pub snap_move: f32,
    pub snap_rotate: f32,
    pub snap_scale: f32,
}

impl Default for Gizmo {
    fn default() -> Self {
        Gizmo { mode: GizmoMode::Move, hovered: None, drag: None, snap_move: 0.5, snap_rotate: 15.0, snap_scale: 0.1 }
    }
}

fn snap(v: f32, step: f32) -> f32 {
    if step > 0.0 {
        (v / step).round() * step
    } else {
        v
    }
}

/// Distance of a pixel to a segment between two pixels.
fn distance_to_segment(p: (f32, f32), a: (f32, f32), b: (f32, f32)) -> f32 {
    let (dx, dy) = (b.0 - a.0, b.1 - a.1);
    let len2 = dx * dx + dy * dy;
    let t = if len2 > 1e-9 { (((p.0 - a.0) * dx + (p.1 - a.1) * dy) / len2).clamp(0.0, 1.0) } else { 0.0 };
    let (cx, cy) = (a.0 + dx * t, a.1 + dy * t);
    ((p.0 - cx).powi(2) + (p.1 - cy).powi(2)).sqrt()
}

/// Where along the line `center + axis * s` the ray comes closest.
fn closest_on_axis(ray: &Ray, center: Vec3, axis: Vec3) -> Option<f32> {
    let w = center - ray.origin;
    let b = axis.dot(ray.direction);
    let denom = 1.0 - b * b;
    if denom.abs() < 1e-5 {
        return None;
    }
    let d = axis.dot(w);
    let e = ray.direction.dot(w);
    Some((b * e - d) / denom)
}

fn ray_plane(ray: &Ray, point: Vec3, normal: Vec3) -> Option<Vec3> {
    let denom = normal.dot(ray.direction);
    if denom.abs() < 1e-6 {
        return None;
    }
    let t = normal.dot(point - ray.origin) / denom;
    if t < 0.0 {
        return None;
    }
    Some(ray.at(t))
}

/// Two directions across an axis (to draw a ring around it).
fn across(axis: Vec3) -> (Vec3, Vec3) {
    let helper = if axis.y.abs() < 0.9 { Vec3::Y } else { Vec3::X };
    let u = axis.cross(helper).normalize();
    let v = axis.cross(u).normalize();
    (u, v)
}

const RING_SEGMENTS: usize = 48;

impl Gizmo {
    pub fn is_dragging(&self) -> bool {
        self.drag.is_some()
    }

    /// The middle and the three axes of the handles, and their length in the world (the same size on screen).
    fn frame(&self, scene: &Scene, id: EntityId, cam: &OrbitCamera) -> Option<(Vec3, [Vec3; 3], f32)> {
        let world = scene.world_matrix(id);
        let center = world.origin();
        let axes = match self.mode {
            GizmoMode::Scale => [
                world.transform_vector(Vec3::X).normalize(),
                world.transform_vector(Vec3::Y).normalize(),
                world.transform_vector(Vec3::Z).normalize(),
            ],
            _ => [Vec3::X, Vec3::Y, Vec3::Z],
        };
        let depth = (center - cam.eye()).dot(cam.forward()).max(cam.near);
        let size = depth * (cam.fov.to_radians() * 0.5).tan() * 0.28;
        Some((center, axes, size))
    }

    /// The segments of the handles of an entity (its rings, arrows or boxes).
    pub fn lines(&self, scene: &Scene, id: EntityId, cam: &OrbitCamera) -> Vec<Line> {
        let Some((center, axes, size)) = self.frame(scene, id, cam) else { return Vec::new() };
        let active = self.drag.as_ref().map(|d| d.handle).or(self.hovered);
        let color = |h: usize| if active == Some(h) { HOVER_COLOR } else { AXIS_COLORS[h] };
        let mut out = Vec::new();
        match self.mode {
            GizmoMode::Move | GizmoMode::Scale => {
                for (h, axis) in axes.iter().enumerate() {
                    let tip = center + *axis * size;
                    out.push(Line { a: center, b: tip, color: color(h) });
                    let (u, v) = across(*axis);
                    let head = size * 0.12;
                    if self.mode == GizmoMode::Move {
                        // a cone made of four lines
                        let base = center + *axis * (size * 0.82);
                        for side in [u, -u, v, -v] {
                            out.push(Line { a: tip, b: base + side * (head * 0.5), color: color(h) });
                        }
                    } else {
                        // a small square at the end
                        let corners = [u + v, u - v, -u - v, -u + v].map(|c| tip + c * (head * 0.4));
                        for k in 0..4 {
                            out.push(Line { a: corners[k], b: corners[(k + 1) % 4], color: color(h) });
                        }
                    }
                }
                // the middle: a small square facing the camera
                let (r, up) = (cam.right(), cam.up());
                let m = size * 0.08;
                let corners = [r + up, r - up, -r - up, -r + up].map(|c| center + c * m);
                for k in 0..4 {
                    out.push(Line { a: corners[k], b: corners[(k + 1) % 4], color: color(3) });
                }
            }
            GizmoMode::Rotate => {
                for (h, axis) in axes.iter().enumerate() {
                    let (u, v) = across(*axis);
                    for k in 0..RING_SEGMENTS {
                        let a0 = k as f32 / RING_SEGMENTS as f32 * std::f32::consts::TAU;
                        let a1 = (k + 1) as f32 / RING_SEGMENTS as f32 * std::f32::consts::TAU;
                        out.push(Line {
                            a: center + (u * a0.cos() + v * a0.sin()) * size,
                            b: center + (u * a1.cos() + v * a1.sin()) * size,
                            color: color(h),
                        });
                    }
                }
            }
        }
        out
    }

    /// The handle under a pixel, if any (and remembers it to light it).
    pub fn hit(&mut self, scene: &Scene, id: EntityId, cam: &OrbitCamera, x: f32, y: f32, width: f32, height: f32) -> Option<usize> {
        let found = self.find(scene, id, cam, x, y, width, height);
        if self.drag.is_none() {
            self.hovered = found;
        }
        found
    }

    fn find(&self, scene: &Scene, id: EntityId, cam: &OrbitCamera, x: f32, y: f32, width: f32, height: f32) -> Option<usize> {
        let (center, axes, size) = self.frame(scene, id, cam)?;
        let p = (x, y);
        let project = |q: Vec3| cam.project(q, width, height).map(|(px, py, _)| (px, py));
        let c = project(center)?;
        let mut best: Option<(usize, f32)> = None;
        let mut consider = |h: usize, d: f32, limit: f32| {
            if d < limit && best.map_or(true, |(_, bd)| d < bd) {
                best = Some((h, d));
            }
        };
        if self.mode != GizmoMode::Rotate {
            let d = ((p.0 - c.0).powi(2) + (p.1 - c.1).powi(2)).sqrt();
            if d < 9.0 {
                return Some(3);
            }
            for (h, axis) in axes.iter().enumerate() {
                if let Some(tip) = project(center + *axis * size) {
                    // the middle (first 15 %) belongs to handle 3
                    if let Some(start) = project(center + *axis * (size * 0.15)) {
                        consider(h, distance_to_segment(p, start, tip), 8.0);
                    }
                }
            }
        } else {
            for (h, axis) in axes.iter().enumerate() {
                let (u, v) = across(*axis);
                let mut previous: Option<(f32, f32)> = None;
                for k in 0..=RING_SEGMENTS {
                    let a = k as f32 / RING_SEGMENTS as f32 * std::f32::consts::TAU;
                    let q = project(center + (u * a.cos() + v * a.sin()) * size);
                    if let (Some(q0), Some(q1)) = (previous, q) {
                        consider(h, distance_to_segment(p, q0, q1), 7.0);
                    }
                    previous = q;
                }
            }
        }
        best.map(|(h, _)| h)
    }

    /// Takes a handle under the mouse. False when there is none.
    pub fn begin(&mut self, scene: &Scene, id: EntityId, cam: &OrbitCamera, x: f32, y: f32, width: f32, height: f32) -> bool {
        let Some(handle) = self.find(scene, id, cam, x, y, width, height) else { return false };
        let Some((center, axes, _)) = self.frame(scene, id, cam) else { return false };
        let Some(entity) = scene.get(id) else { return false };
        let parent_world = entity.parent.map(|p| scene.world_matrix(p)).unwrap_or(Mat4::IDENTITY);
        let Some(parent_inverse) = parent_world.inverse() else { return false };
        let ray = cam.ray(x, y, width, height);
        let axis = if handle < 3 { axes[handle] } else { cam.forward() };
        let mut drag = Drag {
            handle,
            id,
            start: entity.transform.clone(),
            start_world: scene.world_matrix(id),
            parent_inverse,
            center,
            axis,
            start_param: 0.0,
            start_vector: Vec3::ZERO,
            start_pixel: (x, y),
        };
        match (self.mode, handle) {
            (GizmoMode::Move, 3) => drag.start_vector = ray_plane(&ray, center, cam.forward()).unwrap_or(center),
            (GizmoMode::Move, _) | (GizmoMode::Scale, 0..=2) => drag.start_param = closest_on_axis(&ray, center, axis).unwrap_or(0.0),
            (GizmoMode::Rotate, _) => {
                let hit = ray_plane(&ray, center, axis).unwrap_or(center + across(axis).0);
                drag.start_vector = (hit - center).normalize();
            }
            _ => {}
        }
        self.hovered = Some(handle);
        self.drag = Some(drag);
        true
    }

    /// Follows the mouse while a handle is held. `snapping`: by steps (Ctrl held). True when the entity changed.
    pub fn drag(&mut self, scene: &mut Scene, cam: &OrbitCamera, x: f32, y: f32, width: f32, height: f32, snapping: bool) -> bool {
        let Some(d) = self.drag.clone() else { return false };
        let ray = cam.ray(x, y, width, height);
        let mut t = d.start.clone();
        match (self.mode, d.handle) {
            (GizmoMode::Move, handle) => {
                let world_target = if handle < 3 {
                    let Some(s) = closest_on_axis(&ray, d.center, d.axis) else { return false };
                    let mut delta = s - d.start_param;
                    if snapping {
                        delta = snap(delta, self.snap_move);
                    }
                    d.center + d.axis * delta
                } else {
                    let Some(hit) = ray_plane(&ray, d.center, cam.forward()) else { return false };
                    d.center + (hit - d.start_vector)
                };
                t.position = d.parent_inverse.transform_point(world_target);
                if snapping && handle == 3 {
                    t.position = Vec3::new(snap(t.position.x, self.snap_move), snap(t.position.y, self.snap_move), snap(t.position.z, self.snap_move));
                }
            }
            (GizmoMode::Scale, 3) => {
                let mut factor = (1.0 + (x - d.start_pixel.0 - (y - d.start_pixel.1)) / 150.0).max(0.01);
                if snapping {
                    factor = snap(factor, self.snap_scale).max(self.snap_scale);
                }
                t.scale = d.start.scale * factor;
            }
            (GizmoMode::Scale, handle) => {
                let Some(s) = closest_on_axis(&ray, d.center, d.axis) else { return false };
                let base = if d.start_param.abs() > 1e-4 { d.start_param } else { 1.0 };
                let mut value = d.start.scale.component(handle) * (s / base);
                if snapping {
                    value = snap(value, self.snap_scale);
                }
                t.scale.set_component(handle, value);
            }
            (GizmoMode::Rotate, handle) => {
                let Some(hit) = ray_plane(&ray, d.center, d.axis) else { return false };
                let v = (hit - d.center).normalize();
                let mut angle = d.start_vector.cross(v).dot(d.axis).atan2(d.start_vector.dot(v));
                if snapping {
                    angle = snap(angle.to_degrees(), self.snap_rotate).to_radians();
                }
                let turn = match handle {
                    0 => Mat4::rotation_x(angle),
                    1 => Mat4::rotation_y(angle),
                    _ => Mat4::rotation_z(angle),
                };
                let world = Mat4::translation(d.center) * turn * Mat4::translation(-d.center) * d.start_world;
                let local = d.parent_inverse * world;
                t = decompose(&local, &d.start);
                // stays near the angles it had (no jump from 180 to -180 in the fields)
                t.rotation = Vec3::new(
                    nearest_angle(t.rotation.x, d.start.rotation.x),
                    nearest_angle(t.rotation.y, d.start.rotation.y),
                    nearest_angle(t.rotation.z, d.start.rotation.z),
                );
            }
        }
        match scene.get_mut(d.id) {
            Some(e) if e.transform != t => {
                e.transform = t;
                true
            }
            _ => false,
        }
    }

    pub fn end(&mut self) {
        self.drag = None;
    }

    /// Lets go and puts the entity back as it was (Escape).
    pub fn cancel(&mut self, scene: &mut Scene) {
        if let Some(d) = self.drag.take() {
            if let Some(e) = scene.get_mut(d.id) {
                e.transform = d.start;
            }
        }
    }
}

fn nearest_angle(a: f32, reference: f32) -> f32 {
    let mut a = a;
    while a - reference > 180.0 {
        a -= 360.0;
    }
    while a - reference < -180.0 {
        a += 360.0;
    }
    a
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::scene::{Kind, Shape};

    fn setup() -> (Scene, EntityId, OrbitCamera) {
        let mut s = Scene::default();
        let id = s.add("Cube", Kind::mesh(Shape::Cube), None);
        // looking along -Z: X goes right on the screen, Y up
        let cam = OrbitCamera { target: Vec3::ZERO, yaw: 0.0, pitch: 0.0, distance: 10.0, ..Default::default() };
        (s, id, cam)
    }

    #[test]
    fn move_along_x() {
        let (mut s, id, cam) = setup();
        let mut g = Gizmo::default();
        let (w, h) = (800.0, 600.0);
        let (center, axes, size) = g.frame(&s, id, &cam).unwrap();
        let (x, y, _) = cam.project(center + axes[0] * (size * 0.7), w, h).unwrap();
        assert_eq!(g.hit(&s, id, &cam, x, y, w, h), Some(0));
        assert!(g.begin(&s, id, &cam, x, y, w, h));
        // 100 pixels to the right, a little up: only X changes
        assert!(g.drag(&mut s, &cam, x + 100.0, y - 20.0, w, h, false));
        let p = s.get(id).unwrap().transform.position;
        assert!(p.x > 0.5 && p.y.abs() < 1e-4 && p.z.abs() < 1e-4, "{:?}", p);
        g.drag(&mut s, &cam, x + 100.0, y, w, h, true);
        let snapped = s.get(id).unwrap().transform.position.x;
        assert!((snapped / 0.5 - (snapped / 0.5).round()).abs() < 1e-4);
        g.cancel(&mut s);
        assert_eq!(s.get(id).unwrap().transform.position, Vec3::ZERO);
    }

    #[test]
    fn move_under_a_parent() {
        let (mut s, child, cam) = setup();
        let parent = s.add("Parent", Kind::Empty, None);
        s.get_mut(parent).unwrap().transform.scale = Vec3::splat(2.0);
        s.set_parent(child, Some(parent), 0);
        let mut g = Gizmo::default();
        let (w, h) = (800.0, 600.0);
        let (center, axes, size) = g.frame(&s, child, &cam).unwrap();
        let (x, y, _) = cam.project(center + axes[1] * (size * 0.6), w, h).unwrap();
        assert!(g.begin(&s, child, &cam, x, y, w, h));
        g.drag(&mut s, &cam, x, y - 60.0, w, h, false);
        let world = s.world_matrix(child).origin();
        let local = s.get(child).unwrap().transform.position;
        // the world moves twice as far as the local position (the parent is scaled by 2)
        assert!((world.y - local.y * 2.0).abs() < 1e-4 && world.y > 0.1, "{:?} {:?}", world, local);
    }

    #[test]
    fn rotate_around_z_and_scale() {
        let (mut s, id, cam) = setup();
        let (w, h) = (800.0, 600.0);
        let mut g = Gizmo { mode: GizmoMode::Rotate, ..Default::default() };
        let (center, _, size) = g.frame(&s, id, &cam).unwrap();
        // the Z ring faces the camera: take it at 45 degrees (away from the X and Y rings, seen edge on), turn by 90
        let k = std::f32::consts::FRAC_1_SQRT_2;
        let (x0, y0, _) = cam.project(center + Vec3::new(k, k, 0.0) * size, w, h).unwrap();
        assert_eq!(g.hit(&s, id, &cam, x0, y0, w, h), Some(2));
        assert!(g.begin(&s, id, &cam, x0, y0, w, h));
        let (x1, y1, _) = cam.project(center + Vec3::new(-k, k, 0.0) * size, w, h).unwrap();
        g.drag(&mut s, &cam, x1, y1, w, h, false);
        let r = s.get(id).unwrap().transform.rotation;
        assert!((r.z - 90.0).abs() < 1.0 && r.x.abs() < 1e-2 && r.y.abs() < 1e-2, "{:?}", r);
        g.end();

        g.mode = GizmoMode::Scale;
        let (center, axes, size) = g.frame(&s, id, &cam).unwrap();
        // turned by 90 degrees: the local X now points up
        assert!((axes[0] - Vec3::Y).length() < 1e-2);
        let (x, y, _) = cam.project(center + axes[0] * (size * 0.5), w, h).unwrap();
        assert!(g.begin(&s, id, &cam, x, y, w, h));
        let (x2, y2, _) = cam.project(center + axes[0] * size, w, h).unwrap();
        g.drag(&mut s, &cam, x2, y2, w, h, false);
        let sc = s.get(id).unwrap().transform.scale;
        assert!((sc.x - 2.0).abs() < 1e-2 && (sc.y - 1.0).abs() < 1e-4, "{:?}", sc);
    }
}
