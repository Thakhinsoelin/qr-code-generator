//! The camera of the editor: it turns around a point, as in Blender or Unity.

use crate::math::{Mat4, Vec3};

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Ray {
    pub origin: Vec3,
    /// Of length 1.
    pub direction: Vec3,
}

impl Ray {
    pub fn at(&self, t: f32) -> Vec3 {
        self.origin + self.direction * t
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct OrbitCamera {
    pub target: Vec3,
    /// Degrees around Y (0: looking toward -Z).
    pub yaw: f32,
    /// Degrees, negative looks down.
    pub pitch: f32,
    pub distance: f32,
    pub fov: f32,
    pub near: f32,
    pub far: f32,
}

impl Default for OrbitCamera {
    fn default() -> Self {
        OrbitCamera { target: Vec3::new(0.0, 0.5, 0.0), yaw: 30.0, pitch: -25.0, distance: 9.0, fov: 45.0, near: 0.1, far: 1000.0 }
    }
}

impl OrbitCamera {
    /// Where the camera looks, of length 1.
    pub fn forward(&self) -> Vec3 {
        let (yaw, pitch) = (self.yaw.to_radians(), self.pitch.to_radians());
        Vec3::new(-yaw.sin() * pitch.cos(), pitch.sin(), -yaw.cos() * pitch.cos())
    }
    pub fn eye(&self) -> Vec3 {
        self.target - self.forward() * self.distance
    }
    pub fn right(&self) -> Vec3 {
        self.forward().cross(Vec3::Y).normalize()
    }
    pub fn up(&self) -> Vec3 {
        self.right().cross(self.forward())
    }
    pub fn view(&self) -> Mat4 {
        Mat4::look_at(self.eye(), self.target, Vec3::Y)
    }
    pub fn projection(&self, aspect: f32) -> Mat4 {
        Mat4::perspective(self.fov, aspect.max(1e-3), self.near, self.far)
    }
    /// The camera's place and turn as an entity transform (what Bevy is given): Euler YXZ in degrees.
    pub fn rotation_euler(&self) -> Vec3 {
        Vec3::new(self.pitch, self.yaw, 0.0)
    }

    pub fn orbit(&mut self, dx_deg: f32, dy_deg: f32) {
        self.yaw = (self.yaw + dx_deg) % 360.0;
        self.pitch = (self.pitch + dy_deg).clamp(-89.0, 89.0);
    }

    /// Moves the point looked at so that the scene follows the mouse (pixels, in a viewport `height` tall).
    pub fn pan(&mut self, dx_px: f32, dy_px: f32, height: f32) {
        let per_pixel = 2.0 * self.distance * (self.fov.to_radians() * 0.5).tan() / height.max(1.0);
        self.target = self.target - self.right() * (dx_px * per_pixel) + self.up() * (dy_px * per_pixel);
    }

    /// Steps of the wheel: positive comes closer.
    pub fn zoom(&mut self, steps: f32) {
        self.distance = (self.distance * 0.85f32.powf(steps)).clamp(0.2, 500.0);
    }

    /// Looks at a box so that it fills the view.
    pub fn frame(&mut self, min: Vec3, max: Vec3) {
        let center = (min + max) * 0.5;
        let radius = ((max - min).length() * 0.5).max(0.25);
        self.target = center;
        self.distance = (radius / (self.fov.to_radians() * 0.5).sin() * 1.1).clamp(0.5, 500.0);
    }

    /// The ray under a pixel of a viewport of `width` x `height` (y down).
    pub fn ray(&self, x: f32, y: f32, width: f32, height: f32) -> Ray {
        let ndc_x = 2.0 * x / width.max(1.0) - 1.0;
        let ndc_y = 1.0 - 2.0 * y / height.max(1.0);
        let tan = (self.fov.to_radians() * 0.5).tan();
        let aspect = width.max(1.0) / height.max(1.0);
        let direction = (self.forward() + self.right() * (ndc_x * tan * aspect) + self.up() * (ndc_y * tan)).normalize();
        Ray { origin: self.eye(), direction }
    }

    /// The pixel where a point is seen, or None behind the camera. Also returns its depth.
    pub fn project(&self, p: Vec3, width: f32, height: f32) -> Option<(f32, f32, f32)> {
        let view_proj = self.projection(width / height.max(1.0)) * self.view();
        let c = view_proj.transform_point4(p);
        if c[3] <= self.near * 0.5 {
            return None;
        }
        let x = (c[0] / c[3] * 0.5 + 0.5) * width;
        let y = (0.5 - c[1] / c[3] * 0.5) * height;
        Some((x, y, c[3]))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ray_and_project_agree() {
        let cam = OrbitCamera::default();
        let (w, h) = (800.0, 500.0);
        // the middle of the view is the target
        let (x, y, _) = cam.project(cam.target, w, h).unwrap();
        assert!((x - 400.0).abs() < 0.01 && (y - 250.0).abs() < 0.01);
        let p = Vec3::new(1.5, 0.2, -2.0);
        let (px, py, depth) = cam.project(p, w, h).unwrap();
        let ray = cam.ray(px, py, w, h);
        let back = ray.at((p - ray.origin).length());
        assert!((back - p).length() < 1e-3, "{:?} {:?} {}", back, p, depth);
        // behind the camera
        assert!(cam.project(cam.eye() - cam.forward() * 3.0, w, h).is_none());
    }

    #[test]
    fn pan_follows_the_mouse() {
        let mut cam = OrbitCamera::default();
        let (w, h) = (800.0, 600.0);
        let p = cam.target;
        cam.pan(50.0, 0.0, h);
        let (x, _, _) = cam.project(p, w, h).unwrap();
        assert!((x - 450.0).abs() < 0.5, "{}", x);
    }
}
