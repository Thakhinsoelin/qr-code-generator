//! Small 3D math: what the editor needs, with no dependency (Bevy has its own, the renderer converts).

use std::ops::{Add, Mul, Neg, Sub};

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const ZERO: Vec3 = Vec3 { x: 0.0, y: 0.0, z: 0.0 };
    pub const ONE: Vec3 = Vec3 { x: 1.0, y: 1.0, z: 1.0 };
    pub const X: Vec3 = Vec3 { x: 1.0, y: 0.0, z: 0.0 };
    pub const Y: Vec3 = Vec3 { x: 0.0, y: 1.0, z: 0.0 };
    pub const Z: Vec3 = Vec3 { x: 0.0, y: 0.0, z: 1.0 };

    pub const fn new(x: f32, y: f32, z: f32) -> Vec3 {
        Vec3 { x, y, z }
    }
    pub fn splat(v: f32) -> Vec3 {
        Vec3::new(v, v, v)
    }
    pub fn dot(self, o: Vec3) -> f32 {
        self.x * o.x + self.y * o.y + self.z * o.z
    }
    pub fn cross(self, o: Vec3) -> Vec3 {
        Vec3::new(self.y * o.z - self.z * o.y, self.z * o.x - self.x * o.z, self.x * o.y - self.y * o.x)
    }
    pub fn length(self) -> f32 {
        self.dot(self).sqrt()
    }
    pub fn normalize(self) -> Vec3 {
        let l = self.length();
        if l > 1e-12 {
            self * (1.0 / l)
        } else {
            Vec3::ZERO
        }
    }
    pub fn min(self, o: Vec3) -> Vec3 {
        Vec3::new(self.x.min(o.x), self.y.min(o.y), self.z.min(o.z))
    }
    pub fn max(self, o: Vec3) -> Vec3 {
        Vec3::new(self.x.max(o.x), self.y.max(o.y), self.z.max(o.z))
    }
    pub fn component(self, axis: usize) -> f32 {
        match axis {
            0 => self.x,
            1 => self.y,
            _ => self.z,
        }
    }
    pub fn set_component(&mut self, axis: usize, value: f32) {
        match axis {
            0 => self.x = value,
            1 => self.y = value,
            _ => self.z = value,
        }
    }
    pub fn to_array(self) -> [f32; 3] {
        [self.x, self.y, self.z]
    }
}

impl Add for Vec3 {
    type Output = Vec3;
    fn add(self, o: Vec3) -> Vec3 {
        Vec3::new(self.x + o.x, self.y + o.y, self.z + o.z)
    }
}
impl Sub for Vec3 {
    type Output = Vec3;
    fn sub(self, o: Vec3) -> Vec3 {
        Vec3::new(self.x - o.x, self.y - o.y, self.z - o.z)
    }
}
impl Mul<f32> for Vec3 {
    type Output = Vec3;
    fn mul(self, s: f32) -> Vec3 {
        Vec3::new(self.x * s, self.y * s, self.z * s)
    }
}
impl Neg for Vec3 {
    type Output = Vec3;
    fn neg(self) -> Vec3 {
        Vec3::new(-self.x, -self.y, -self.z)
    }
}

/// A 4x4 matrix, column-major (as Bevy's `Mat4::from_cols_array`): `m[column * 4 + row]`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Mat4 {
    pub m: [f32; 16],
}

impl Mat4 {
    pub const IDENTITY: Mat4 = Mat4 { m: [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0] };

    pub fn get(&self, row: usize, column: usize) -> f32 {
        self.m[column * 4 + row]
    }

    pub fn set(&mut self, row: usize, column: usize, v: f32) {
        self.m[column * 4 + row] = v;
    }

    pub fn translation(t: Vec3) -> Mat4 {
        let mut r = Mat4::IDENTITY;
        r.set(0, 3, t.x);
        r.set(1, 3, t.y);
        r.set(2, 3, t.z);
        r
    }

    pub fn scale(s: Vec3) -> Mat4 {
        let mut r = Mat4::IDENTITY;
        r.set(0, 0, s.x);
        r.set(1, 1, s.y);
        r.set(2, 2, s.z);
        r
    }

    pub fn rotation_x(radians: f32) -> Mat4 {
        let (s, c) = radians.sin_cos();
        let mut r = Mat4::IDENTITY;
        r.set(1, 1, c);
        r.set(1, 2, -s);
        r.set(2, 1, s);
        r.set(2, 2, c);
        r
    }

    pub fn rotation_y(radians: f32) -> Mat4 {
        let (s, c) = radians.sin_cos();
        let mut r = Mat4::IDENTITY;
        r.set(0, 0, c);
        r.set(0, 2, s);
        r.set(2, 0, -s);
        r.set(2, 2, c);
        r
    }

    pub fn rotation_z(radians: f32) -> Mat4 {
        let (s, c) = radians.sin_cos();
        let mut r = Mat4::IDENTITY;
        r.set(0, 0, c);
        r.set(0, 1, -s);
        r.set(1, 0, s);
        r.set(1, 1, c);
        r
    }

    /// Euler angles in degrees, applied as Bevy's `EulerRot::YXZ`: R = Ry * Rx * Rz.
    pub fn rotation_euler_deg(e: Vec3) -> Mat4 {
        Mat4::rotation_y(e.y.to_radians()) * Mat4::rotation_x(e.x.to_radians()) * Mat4::rotation_z(e.z.to_radians())
    }

    /// Translation * Rotation * Scale.
    pub fn from_trs(t: Vec3, euler_deg: Vec3, s: Vec3) -> Mat4 {
        Mat4::translation(t) * Mat4::rotation_euler_deg(euler_deg) * Mat4::scale(s)
    }

    pub fn transform_point(&self, p: Vec3) -> Vec3 {
        let w = self.get(3, 0) * p.x + self.get(3, 1) * p.y + self.get(3, 2) * p.z + self.get(3, 3);
        let x = self.get(0, 0) * p.x + self.get(0, 1) * p.y + self.get(0, 2) * p.z + self.get(0, 3);
        let y = self.get(1, 0) * p.x + self.get(1, 1) * p.y + self.get(1, 2) * p.z + self.get(1, 3);
        let z = self.get(2, 0) * p.x + self.get(2, 1) * p.y + self.get(2, 2) * p.z + self.get(2, 3);
        if w.abs() > 1e-12 && (w - 1.0).abs() > 1e-6 {
            Vec3::new(x / w, y / w, z / w)
        } else {
            Vec3::new(x, y, z)
        }
    }

    /// (x, y, z, w) of a point, without the division (projections).
    pub fn transform_point4(&self, p: Vec3) -> [f32; 4] {
        let mut out = [0.0; 4];
        for (row, value) in out.iter_mut().enumerate() {
            *value = self.get(row, 0) * p.x + self.get(row, 1) * p.y + self.get(row, 2) * p.z + self.get(row, 3);
        }
        out
    }

    pub fn transform_vector(&self, v: Vec3) -> Vec3 {
        Vec3::new(
            self.get(0, 0) * v.x + self.get(0, 1) * v.y + self.get(0, 2) * v.z,
            self.get(1, 0) * v.x + self.get(1, 1) * v.y + self.get(1, 2) * v.z,
            self.get(2, 0) * v.x + self.get(2, 1) * v.y + self.get(2, 2) * v.z,
        )
    }

    pub fn origin(&self) -> Vec3 {
        Vec3::new(self.get(0, 3), self.get(1, 3), self.get(2, 3))
    }

    /// The inverse, or None for a matrix that has none (a scale of 0).
    pub fn inverse(&self) -> Option<Mat4> {
        let m = &self.m;
        let mut inv = [0f32; 16];
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
        let det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if det.abs() < 1e-12 {
            return None;
        }
        let d = 1.0 / det;
        for v in inv.iter_mut() {
            *v *= d;
        }
        Some(Mat4 { m: inv })
    }

    /// A camera at `eye` looking at `target` (right-handed, -Z forward, as Bevy).
    pub fn look_at(eye: Vec3, target: Vec3, up: Vec3) -> Mat4 {
        let f = (target - eye).normalize();
        let s = f.cross(up).normalize();
        let u = s.cross(f);
        let mut r = Mat4::IDENTITY;
        r.set(0, 0, s.x);
        r.set(0, 1, s.y);
        r.set(0, 2, s.z);
        r.set(1, 0, u.x);
        r.set(1, 1, u.y);
        r.set(1, 2, u.z);
        r.set(2, 0, -f.x);
        r.set(2, 1, -f.y);
        r.set(2, 2, -f.z);
        r.set(0, 3, -s.dot(eye));
        r.set(1, 3, -u.dot(eye));
        r.set(2, 3, f.dot(eye));
        r
    }

    /// A perspective projection to clip space (OpenGL depth -1..1: only x, y and the sign of w are used).
    pub fn perspective(fov_y_deg: f32, aspect: f32, near: f32, far: f32) -> Mat4 {
        let f = 1.0 / (fov_y_deg.to_radians() * 0.5).tan();
        let mut r = Mat4 { m: [0.0; 16] };
        r.set(0, 0, f / aspect);
        r.set(1, 1, f);
        r.set(2, 2, (far + near) / (near - far));
        r.set(2, 3, 2.0 * far * near / (near - far));
        r.set(3, 2, -1.0);
        r
    }
}

impl Mul for Mat4 {
    type Output = Mat4;
    fn mul(self, o: Mat4) -> Mat4 {
        let mut r = Mat4 { m: [0.0; 16] };
        for row in 0..4 {
            for column in 0..4 {
                let mut sum = 0.0;
                for k in 0..4 {
                    sum += self.get(row, k) * o.get(k, column);
                }
                r.set(row, column, sum);
            }
        }
        r
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn close(a: Vec3, b: Vec3) -> bool {
        (a - b).length() < 1e-4
    }

    #[test]
    fn trs_and_inverse() {
        let m = Mat4::from_trs(Vec3::new(1.0, 2.0, 3.0), Vec3::new(30.0, 45.0, 10.0), Vec3::new(2.0, 1.0, 0.5));
        let p = Vec3::new(0.3, -0.7, 2.0);
        let back = m.inverse().unwrap().transform_point(m.transform_point(p));
        assert!(close(back, p));
        assert!(close(m.origin(), Vec3::new(1.0, 2.0, 3.0)));
        assert!(Mat4::scale(Vec3::new(0.0, 1.0, 1.0)).inverse().is_none());
    }

    #[test]
    fn rotation_y_turns_x_to_minus_z() {
        let v = Mat4::rotation_euler_deg(Vec3::new(0.0, 90.0, 0.0)).transform_vector(Vec3::X);
        assert!(close(v, Vec3::new(0.0, 0.0, -1.0)));
    }

    #[test]
    fn look_at_puts_target_in_front() {
        let view = Mat4::look_at(Vec3::new(0.0, 0.0, 5.0), Vec3::ZERO, Vec3::Y);
        let p = view.transform_point(Vec3::ZERO);
        assert!(close(p, Vec3::new(0.0, 0.0, -5.0)));
    }
}
