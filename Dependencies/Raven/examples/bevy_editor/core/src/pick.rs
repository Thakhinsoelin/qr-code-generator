//! What is under the mouse: the ray of the pixel against the box of each entity.

use crate::camera::Ray;
use crate::math::{Mat4, Vec3};
use crate::scene::{EntityId, Scene};

/// Distance along the ray where it enters the box, or None.
pub fn ray_box(ray: &Ray, min: Vec3, max: Vec3) -> Option<f32> {
    let mut t0 = f32::NEG_INFINITY;
    let mut t1 = f32::INFINITY;
    for axis in 0..3 {
        let o = ray.origin.component(axis);
        let d = ray.direction.component(axis);
        let (lo, hi) = (min.component(axis), max.component(axis));
        if d.abs() < 1e-12 {
            if o < lo || o > hi {
                return None;
            }
            continue;
        }
        let (mut a, mut b) = ((lo - o) / d, (hi - o) / d);
        if a > b {
            std::mem::swap(&mut a, &mut b);
        }
        t0 = t0.max(a);
        t1 = t1.min(b);
        if t0 > t1 {
            return None;
        }
    }
    if t1 < 0.0 {
        return None;
    }
    Some(t0.max(0.0))
}

/// The ray in the space of a matrix (for the box of an entity, in its own space).
/// The distances along it stay those of the world.
pub fn ray_into(ray: &Ray, world: &Mat4) -> Option<(Ray, f32)> {
    let inverse = world.inverse()?;
    let origin = inverse.transform_point(ray.origin);
    let d = inverse.transform_vector(ray.direction);
    let len = d.length();
    if len < 1e-12 {
        return None;
    }
    Some((Ray { origin, direction: d * (1.0 / len) }, len))
}

/// The closest entity shown under the ray, with its distance.
pub fn pick(scene: &Scene, ray: &Ray) -> Option<(EntityId, f32)> {
    let mut best: Option<(EntityId, f32)> = None;
    for e in &scene.entities {
        if !scene.is_shown(e.id) {
            continue;
        }
        let world = scene.world_matrix(e.id);
        let Some((local, scale)) = ray_into(ray, &world) else { continue };
        let (min, max) = e.kind.local_bounds();
        if let Some(t_local) = ray_box(&local, min, max) {
            let t = t_local / scale;
            if best.map_or(true, |(_, bt)| t < bt) {
                best = Some((e.id, t));
            }
        }
    }
    best
}

/// The box around an entity and what is under it, in the world (to frame it).
pub fn world_bounds(scene: &Scene, id: EntityId) -> Option<(Vec3, Vec3)> {
    let mut out: Option<(Vec3, Vec3)> = None;
    for e in scene.subtree(id) {
        let entity = scene.get(e)?;
        let world = scene.world_matrix(e);
        let (min, max) = entity.kind.local_bounds();
        for i in 0..8 {
            let corner = Vec3::new(
                if i & 1 == 0 { min.x } else { max.x },
                if i & 2 == 0 { min.y } else { max.y },
                if i & 4 == 0 { min.z } else { max.z },
            );
            let p = world.transform_point(corner);
            out = Some(match out {
                Some((lo, hi)) => (lo.min(p), hi.max(p)),
                None => (p, p),
            });
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::camera::OrbitCamera;
    use crate::scene::{Kind, Shape};

    #[test]
    fn picks_the_closest() {
        let mut s = Scene::default();
        let far = s.add("Far", Kind::mesh(Shape::Cube), None);
        let near = s.add("Near", Kind::mesh(Shape::Cube), None);
        s.get_mut(far).unwrap().transform.position = Vec3::new(0.0, 0.0, -5.0);
        s.get_mut(near).unwrap().transform.position = Vec3::new(0.0, 0.0, 0.0);
        s.get_mut(near).unwrap().transform.scale = Vec3::splat(2.0);
        let ray = Ray { origin: Vec3::new(0.0, 0.0, 10.0), direction: Vec3::new(0.0, 0.0, -1.0) };
        let (id, t) = pick(&s, &ray).unwrap();
        assert_eq!(id, near);
        assert!((t - 9.0).abs() < 1e-3, "{}", t);
        s.get_mut(near).unwrap().visible = false;
        assert_eq!(pick(&s, &ray).unwrap().0, far);
    }

    #[test]
    fn picks_through_a_rotated_parent() {
        let mut s = Scene::default();
        let parent = s.add("Parent", Kind::Empty, None);
        s.get_mut(parent).unwrap().transform.rotation = Vec3::new(0.0, 90.0, 0.0);
        let child = s.add("Child", Kind::mesh(Shape::Cube), Some(parent));
        // 3 along the local X of the parent, which is turned to -Z in the world
        s.get_mut(child).unwrap().transform.position = Vec3::new(3.0, 0.0, 0.0);
        // from behind (looking toward +Z), so the parent at the origin is further than the child
        let cam = OrbitCamera { target: Vec3::new(0.0, 0.0, -3.0), yaw: 180.0, pitch: 0.0, distance: 6.0, ..Default::default() };
        let ray = cam.ray(400.0, 300.0, 800.0, 600.0);
        assert_eq!(pick(&s, &ray).map(|p| p.0), Some(child));
        let (lo, hi) = world_bounds(&s, parent).unwrap();
        // the child's box (z -3.5..-2.5) and the parent's own (an empty: -0.15..0.15)
        assert!((lo.z + 3.5).abs() < 1e-3 && (hi.z - 0.15).abs() < 1e-3, "{:?} {:?}", lo, hi);
    }
}
