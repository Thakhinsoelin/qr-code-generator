//! What draws the scene into the viewport.
//!
//! The editor asks a [`SceneRenderer`] for a picture of the scene (Bevy in the program,
//! [`SoftwareRenderer`] without it), then draws on top what belongs to the editor: the
//! handles of the gizmo, the box of the selection, the icons of lights and cameras.

use editor_core::math::{Mat4, Vec3};
use editor_core::{EntityId, Kind, Line, OrbitCamera, Scene, Shape};

/// A picture: RGBA8, rows from the top, `width * 4` bytes per row.
#[derive(Clone, Debug, Default)]
pub struct Frame {
    pub pixels: Vec<u8>,
    pub width: u32,
    pub height: u32,
}

impl Frame {
    pub fn new(width: u32, height: u32) -> Frame {
        Frame { pixels: vec![0; (width * height * 4) as usize], width, height }
    }
}

/// What a renderer is asked to draw.
pub struct RenderInput<'a> {
    pub scene: &'a Scene,
    pub selection: Option<EntityId>,
    pub camera: &'a OrbitCamera,
    pub show_grid: bool,
    /// In pixels.
    pub width: u32,
    pub height: u32,
}

pub trait SceneRenderer {
    /// Shown in the status bar ("Bevy 0.16", "Software").
    fn name(&self) -> String;
    /// Asks for a picture of the scene as it is now. It may come later (Bevy reads it back from the GPU).
    fn request(&mut self, input: &RenderInput);
    /// The newest picture, once, when there is one.
    fn poll(&mut self) -> Option<Frame>;
    /// Called at every turn of the loop of the interface: the time to work (Bevy runs a frame).
    fn tick(&mut self) {}
    /// True while a picture is on its way: the interface then turns without waiting for events.
    fn busy(&self) -> bool {
        false
    }
}

// ---------------------------------------------------------------------------
//  The shapes, as triangles (the same sizes as Bevy's primitives)
// ---------------------------------------------------------------------------

pub fn triangles(shape: Shape) -> Vec<[Vec3; 3]> {
    use std::f32::consts::{PI, TAU};
    let mut out = Vec::new();
    fn quad(out: &mut Vec<[Vec3; 3]>, a: Vec3, b: Vec3, c: Vec3, d: Vec3) {
        out.push([a, b, c]);
        out.push([a, c, d]);
    }
    match shape {
        Shape::Cube => {
            let h = 0.5;
            let p = |x: f32, y: f32, z: f32| Vec3::new(x * h, y * h, z * h);
            quad(&mut out, p(-1., -1., 1.), p(1., -1., 1.), p(1., 1., 1.), p(-1., 1., 1.));
            quad(&mut out, p(1., -1., -1.), p(-1., -1., -1.), p(-1., 1., -1.), p(1., 1., -1.));
            quad(&mut out, p(1., -1., 1.), p(1., -1., -1.), p(1., 1., -1.), p(1., 1., 1.));
            quad(&mut out, p(-1., -1., -1.), p(-1., -1., 1.), p(-1., 1., 1.), p(-1., 1., -1.));
            quad(&mut out, p(-1., 1., 1.), p(1., 1., 1.), p(1., 1., -1.), p(-1., 1., -1.));
            quad(&mut out, p(-1., -1., -1.), p(1., -1., -1.), p(1., -1., 1.), p(-1., -1., 1.));
        }
        Shape::Plane => {
            // in small squares: the light is worked out per triangle, a big one would show it
            let n = 10;
            let p = |i: usize, j: usize| Vec3::new(i as f32 / n as f32 - 0.5, 0.0, 0.5 - j as f32 / n as f32);
            for i in 0..n {
                for j in 0..n {
                    quad(&mut out, p(i, j), p(i + 1, j), p(i + 1, j + 1), p(i, j + 1));
                }
            }
        }
        Shape::Sphere => {
            let (rings, sectors) = (12, 20);
            let at = |r: usize, s: usize| {
                let phi = PI * r as f32 / rings as f32;
                let theta = TAU * s as f32 / sectors as f32;
                Vec3::new(phi.sin() * theta.cos(), phi.cos(), -phi.sin() * theta.sin()) * 0.5
            };
            for r in 0..rings {
                for s in 0..sectors {
                    quad(&mut out, at(r, s), at(r + 1, s), at(r + 1, s + 1), at(r, s + 1));
                }
            }
        }
        Shape::Cylinder => {
            let n = 24;
            let at = |k: usize, y: f32| {
                let t = TAU * k as f32 / n as f32;
                Vec3::new(t.cos() * 0.5, y, -t.sin() * 0.5)
            };
            for k in 0..n {
                quad(&mut out, at(k, -0.5), at(k + 1, -0.5), at(k + 1, 0.5), at(k, 0.5));
                out.push([Vec3::new(0.0, 0.5, 0.0), at(k, 0.5), at(k + 1, 0.5)]);
                out.push([Vec3::new(0.0, -0.5, 0.0), at(k + 1, -0.5), at(k, -0.5)]);
            }
        }
        Shape::Capsule => {
            // radius 0.5, the middle 1 tall: 2 in all
            let (rings, sectors) = (12, 20);
            let at = |r: usize, s: usize| {
                // the equator twice: once for each half, the side between them
                let (k, offset) = if r <= rings / 2 { (r, 0.5) } else { (r - 1, -0.5) };
                let phi = PI * k as f32 / rings as f32;
                let theta = TAU * s as f32 / sectors as f32;
                Vec3::new(phi.sin() * theta.cos() * 0.5, phi.cos() * 0.5 + offset, -phi.sin() * theta.sin() * 0.5)
            };
            for r in 0..=rings {
                for s in 0..sectors {
                    quad(&mut out, at(r, s), at(r + 1, s), at(r + 1, s + 1), at(r, s + 1));
                }
            }
        }
        Shape::Torus => {
            let (major, minor) = (0.375, 0.125);
            let (n, m) = (32, 12);
            let at = |i: usize, j: usize| {
                let u = TAU * i as f32 / n as f32;
                let v = TAU * j as f32 / m as f32;
                let r = major + minor * v.cos();
                Vec3::new(r * u.cos(), minor * v.sin(), -r * u.sin())
            };
            for i in 0..n {
                for j in 0..m {
                    quad(&mut out, at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
                }
            }
        }
    }
    out
}

// ---------------------------------------------------------------------------
//  Drawing into a frame
// ---------------------------------------------------------------------------

/// A frame with its depths, and the camera it is seen through.
pub struct Canvas<'a> {
    pub frame: &'a mut Frame,
    depth: Vec<f32>,
    view: Mat4,
    proj: Mat4,
    near: f32,
}

fn to_byte(v: f32) -> u8 {
    (v.clamp(0.0, 1.0) * 255.0 + 0.5) as u8
}

/// Linear light to the sRGB of the screen.
pub fn encode(v: f32) -> f32 {
    let v = v.clamp(0.0, 1.0);
    if v <= 0.0031308 {
        v * 12.92
    } else {
        1.055 * v.powf(1.0 / 2.4) - 0.055
    }
}

pub fn decode(v: f32) -> f32 {
    if v <= 0.04045 {
        v / 12.92
    } else {
        ((v + 0.055) / 1.055).powf(2.4)
    }
}

impl<'a> Canvas<'a> {
    pub fn new(frame: &'a mut Frame, camera: &OrbitCamera, depth_test: bool) -> Canvas<'a> {
        let n = (frame.width * frame.height) as usize;
        let aspect = frame.width as f32 / frame.height.max(1) as f32;
        Canvas {
            depth: if depth_test { vec![f32::INFINITY; n] } else { Vec::new() },
            frame,
            view: camera.view(),
            proj: camera.projection(aspect),
            near: camera.near,
        }
    }

    pub fn clear(&mut self, color: [f32; 3]) {
        let px = [to_byte(color[0]), to_byte(color[1]), to_byte(color[2]), 255];
        for chunk in self.frame.pixels.chunks_exact_mut(4) {
            chunk.copy_from_slice(&px);
        }
        for d in self.depth.iter_mut() {
            *d = f32::INFINITY;
        }
    }

    fn put(&mut self, x: i32, y: i32, z: f32, color: [f32; 4]) {
        if x < 0 || y < 0 || x >= self.frame.width as i32 || y >= self.frame.height as i32 {
            return;
        }
        let i = (y as u32 * self.frame.width + x as u32) as usize;
        if !self.depth.is_empty() {
            if z > self.depth[i] {
                return;
            }
            if color[3] >= 1.0 {
                self.depth[i] = z;
            }
        }
        let p = &mut self.frame.pixels[i * 4..i * 4 + 4];
        let a = color[3].clamp(0.0, 1.0);
        for k in 0..3 {
            let dst = p[k] as f32 / 255.0;
            p[k] = to_byte(dst * (1.0 - a) + color[k] * a);
        }
        p[3] = 255;
    }

    /// A point of the view space to the screen: (x, y, depth).
    fn screen(&self, v: Vec3) -> (f32, f32, f32) {
        let c = self.proj.transform_point4(v);
        let w = c[3];
        ((c[0] / w * 0.5 + 0.5) * self.frame.width as f32, (0.5 - c[1] / w * 0.5) * self.frame.height as f32, w)
    }

    /// Cuts a segment at the near plane (view space: in front is z < -near).
    fn clip_segment(&self, a: Vec3, b: Vec3) -> Option<(Vec3, Vec3)> {
        let limit = -self.near;
        let (ina, inb) = (a.z <= limit, b.z <= limit);
        match (ina, inb) {
            (true, true) => Some((a, b)),
            (false, false) => None,
            _ => {
                let t = (limit - a.z) / (b.z - a.z);
                let m = a + (b - a) * t;
                if ina {
                    Some((a, m))
                } else {
                    Some((m, b))
                }
            }
        }
    }

    /// A segment of the world, `thickness` pixels wide.
    pub fn line(&mut self, a: Vec3, b: Vec3, color: [f32; 4], thickness: i32) {
        let (va, vb) = (self.view.transform_point(a), self.view.transform_point(b));
        let Some((va, vb)) = self.clip_segment(va, vb) else { return };
        let (x0, y0, z0) = self.screen(va);
        let (x1, y1, z1) = self.screen(vb);
        let steps = ((x1 - x0).abs().max((y1 - y0).abs()).ceil() as i32).clamp(1, 20000);
        let r = thickness / 2;
        for s in 0..=steps {
            let t = s as f32 / steps as f32;
            let (x, y) = (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
            // depth in perspective: 1/w goes straight across the screen
            let z = 1.0 / ((1.0 - t) / z0 + t / z1) - 0.01;
            for dy in -r..=(thickness - 1 - r) {
                for dx in -r..=(thickness - 1 - r) {
                    self.put(x as i32 + dx, y as i32 + dy, z, color);
                }
            }
        }
    }

    /// A triangle of the view space with one color, depth tested.
    fn triangle_view(&mut self, v: [Vec3; 3], color: [f32; 4]) {
        // cut at the near plane: 0, 1 or 2 triangles
        let limit = -self.near;
        let mut poly: Vec<Vec3> = Vec::with_capacity(4);
        for i in 0..3 {
            let (a, b) = (v[i], v[(i + 1) % 3]);
            let (ina, inb) = (a.z <= limit, b.z <= limit);
            if ina {
                poly.push(a);
            }
            if ina != inb {
                let t = (limit - a.z) / (b.z - a.z);
                poly.push(a + (b - a) * t);
            }
        }
        if poly.len() < 3 {
            return;
        }
        let s: Vec<(f32, f32, f32)> = poly.iter().map(|p| self.screen(*p)).collect();
        for k in 1..s.len() - 1 {
            self.raster(s[0], s[k], s[k + 1], color);
        }
    }

    fn raster(&mut self, a: (f32, f32, f32), b: (f32, f32, f32), c: (f32, f32, f32), color: [f32; 4]) {
        let area = (b.0 - a.0) * (c.1 - a.1) - (b.1 - a.1) * (c.0 - a.0);
        if area.abs() < 1e-8 {
            return;
        }
        let min_x = a.0.min(b.0).min(c.0).floor().max(0.0) as i32;
        let max_x = a.0.max(b.0).max(c.0).ceil().min(self.frame.width as f32 - 1.0) as i32;
        let min_y = a.1.min(b.1).min(c.1).floor().max(0.0) as i32;
        let max_y = a.1.max(b.1).max(c.1).ceil().min(self.frame.height as f32 - 1.0) as i32;
        let (ia, ib, ic) = (1.0 / a.2, 1.0 / b.2, 1.0 / c.2);
        for y in min_y..=max_y {
            let py = y as f32 + 0.5;
            for x in min_x..=max_x {
                let px = x as f32 + 0.5;
                let w0 = ((b.0 - px) * (c.1 - py) - (b.1 - py) * (c.0 - px)) / area;
                let w1 = ((c.0 - px) * (a.1 - py) - (c.1 - py) * (a.0 - px)) / area;
                let w2 = 1.0 - w0 - w1;
                if w0 < 0.0 || w1 < 0.0 || w2 < 0.0 {
                    continue;
                }
                let z = 1.0 / (w0 * ia + w1 * ib + w2 * ic);
                self.put(x, y, z, color);
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  The software renderer: flat shaded triangles and a grid
// ---------------------------------------------------------------------------

/// Draws the scene on the CPU: enough to edit without a GPU (and in the tests).
#[derive(Default)]
pub struct SoftwareRenderer {
    pending: Option<Frame>,
    meshes: Vec<(Shape, Vec<[Vec3; 3]>)>,
}

impl SoftwareRenderer {
    pub fn new() -> SoftwareRenderer {
        SoftwareRenderer { pending: None, meshes: Shape::ALL.iter().map(|s| (*s, triangles(*s))).collect() }
    }

    pub fn draw(&self, input: &RenderInput) -> Frame {
        let mut frame = Frame::new(input.width.max(1), input.height.max(1));
        let scene = input.scene;
        let bg = scene.background;
        let mut canvas = Canvas::new(&mut frame, input.camera, true);
        canvas.clear(bg);

        // the lights, in the world
        let mut suns: Vec<(Vec3, [f32; 3], f32)> = Vec::new();
        let mut points: Vec<(Vec3, [f32; 3], f32, f32)> = Vec::new();
        for e in &scene.entities {
            if !scene.is_shown(e.id) {
                continue;
            }
            let world = scene.world_matrix(e.id);
            match &e.kind {
                // a directional light shines along its -Z
                Kind::DirectionalLight { color, illuminance, .. } => {
                    suns.push(((-world.transform_vector(Vec3::Z)).normalize(), *color, (illuminance / 10_000.0).min(3.0)))
                }
                Kind::PointLight { color, intensity, range, .. } => points.push((world.origin(), *color, intensity / 1_000_000.0, *range)),
                _ => {}
            }
        }
        let ambient = (scene.ambient_brightness / 1000.0).clamp(0.0, 1.0);
        let ambient = [scene.ambient[0] * ambient, scene.ambient[1] * ambient, scene.ambient[2] * ambient];
        let no_light = suns.is_empty() && points.is_empty();
        let camera_world_to_view = input.camera.view();

        for e in &scene.entities {
            let Kind::Mesh { shape, color, .. } = &e.kind else { continue };
            if !scene.is_shown(e.id) {
                continue;
            }
            let world = scene.world_matrix(e.id);
            let to_view = camera_world_to_view * world;
            let base = [decode(color[0]), decode(color[1]), decode(color[2])];
            let tris = &self.meshes.iter().find(|(s, _)| s == shape).unwrap().1;
            for t in tris {
                let w = [world.transform_point(t[0]), world.transform_point(t[1]), world.transform_point(t[2])];
                let normal = (w[1] - w[0]).cross(w[2] - w[0]).normalize();
                let center = (w[0] + w[1] + w[2]) * (1.0 / 3.0);
                // both faces are lit (planes are seen from below too)
                let eye = input.camera.eye();
                let n = if normal.dot(eye - center) < 0.0 { -normal } else { normal };
                let mut light = ambient;
                if no_light {
                    // a head light, so that an unlit scene can be seen
                    let l = n.dot((eye - center).normalize()).max(0.0) * 0.8 + 0.2;
                    light = [l, l, l];
                }
                for (dir, c, strength) in &suns {
                    let k = n.dot(-*dir).max(0.0) * strength;
                    for i in 0..3 {
                        light[i] += c[i] * k;
                    }
                }
                for (pos, c, strength, range) in &points {
                    let to = *pos - center;
                    let d = to.length().max(1e-3);
                    if d > *range {
                        continue;
                    }
                    let falloff = (1.0 - d / range).powi(2) / (1.0 + d * d * 0.05);
                    let k = n.dot(to * (1.0 / d)).max(0.0) * strength * falloff * 3.0;
                    for i in 0..3 {
                        light[i] += c[i] * k;
                    }
                }
                let lit = [encode(base[0] * light[0]), encode(base[1] * light[1]), encode(base[2] * light[2]), 1.0];
                let v = [to_view.transform_point(t[0]), to_view.transform_point(t[1]), to_view.transform_point(t[2])];
                canvas.triangle_view(v, lit);
            }
        }
        if input.show_grid {
            draw_grid(&mut canvas, input.camera);
        }
        drop(canvas);
        frame
    }
}

/// Lines every meter on the ground, X in red and Z in blue.
pub fn draw_grid(canvas: &mut Canvas, camera: &OrbitCamera) {
    let extent = ((camera.distance * 2.0).ceil() as i32).clamp(10, 100);
    let cx = camera.target.x.round() as i32;
    let cz = camera.target.z.round() as i32;
    for i in -extent..=extent {
        let (x, z) = ((cx + i) as f32, (cz + i) as f32);
        let major = |v: i32| if v % 10 == 0 { 0.32 } else { 0.16 };
        let color_x = if cz + i == 0 { [0.86, 0.3, 0.32, 0.9] } else { [1.0, 1.0, 1.0, major(cz + i)] };
        let color_z = if cx + i == 0 { [0.3, 0.5, 0.95, 0.9] } else { [1.0, 1.0, 1.0, major(cx + i)] };
        canvas.line(Vec3::new((cx - extent) as f32, 0.0, z), Vec3::new((cx + extent) as f32, 0.0, z), color_x, 1);
        canvas.line(Vec3::new(x, 0.0, (cz - extent) as f32), Vec3::new(x, 0.0, (cz + extent) as f32), color_z, 1);
    }
}

impl SceneRenderer for SoftwareRenderer {
    fn name(&self) -> String {
        "Software".into()
    }
    fn request(&mut self, input: &RenderInput) {
        self.pending = Some(self.draw(input));
    }
    fn poll(&mut self) -> Option<Frame> {
        self.pending.take()
    }
}

// ---------------------------------------------------------------------------
//  What the editor draws on top of any renderer
// ---------------------------------------------------------------------------

pub const SELECTION_COLOR: [f32; 4] = [1.0, 0.62, 0.2, 1.0];
const ICON_COLOR: [f32; 4] = [0.95, 0.95, 0.75, 0.9];

/// The box of the selection, the icons of what has no mesh, and the gizmo: lines of the world.
pub fn overlay_lines(scene: &Scene, selection: Option<EntityId>, camera: &OrbitCamera, gizmo: &[Line]) -> Vec<Line> {
    let mut out = Vec::new();
    for e in &scene.entities {
        if !scene.is_shown(e.id) {
            continue;
        }
        let world = scene.world_matrix(e.id);
        let o = world.origin();
        // the icons keep their size on the screen
        let size = ((o - camera.eye()).dot(camera.forward()).max(camera.near) * 0.04).max(0.05);
        let color = if Some(e.id) == selection { SELECTION_COLOR } else { ICON_COLOR };
        let mut push = |a: Vec3, b: Vec3| out.push(Line { a, b, color });
        match &e.kind {
            Kind::PointLight { .. } => {
                for axis in [Vec3::X, Vec3::Y, Vec3::Z, (Vec3::X + Vec3::Y).normalize(), (Vec3::X - Vec3::Z).normalize()] {
                    push(o - axis * size, o + axis * size);
                }
            }
            Kind::DirectionalLight { .. } => {
                let dir = world.transform_vector(-Vec3::Z).normalize();
                let side = world.transform_vector(Vec3::X).normalize();
                let up = world.transform_vector(Vec3::Y).normalize();
                for k in 0..8 {
                    let a = k as f32 / 8.0 * std::f32::consts::TAU;
                    let p = o + (side * a.cos() + up * a.sin()) * size;
                    push(p, p + dir * (size * 3.0));
                }
            }
            Kind::Camera { fov } => {
                let f = world.transform_vector(-Vec3::Z).normalize();
                let r = world.transform_vector(Vec3::X).normalize();
                let u = world.transform_vector(Vec3::Y).normalize();
                let len = size * 4.0;
                let half = (fov.to_radians() * 0.5).tan() * len;
                let corners = [r * half * 1.5 + u * half, r * half * 1.5 - u * half, -r * half * 1.5 - u * half, -r * half * 1.5 + u * half].map(|c| o + f * len + c);
                for k in 0..4 {
                    push(o, corners[k]);
                    push(corners[k], corners[(k + 1) % 4]);
                }
                push(o + f * len + u * (half * 1.2) - r * (half * 0.4), o + f * len + u * (half * 1.7));
                push(o + f * len + u * (half * 1.7), o + f * len + u * (half * 1.2) + r * (half * 0.4));
            }
            Kind::Empty => {
                for axis in [Vec3::X, Vec3::Y, Vec3::Z] {
                    let d = world.transform_vector(axis).normalize() * size;
                    push(o - d, o + d);
                }
            }
            Kind::Mesh { .. } => {}
        }
    }
    if let Some(id) = selection {
        if let Some(e) = scene.get(id) {
            if matches!(e.kind, Kind::Mesh { .. }) && scene.is_shown(id) {
                let world = scene.world_matrix(id);
                let (min, max) = e.kind.local_bounds();
                let c = |i: usize| {
                    world.transform_point(Vec3::new(
                        if i & 1 == 0 { min.x } else { max.x },
                        if i & 2 == 0 { min.y } else { max.y },
                        if i & 4 == 0 { min.z } else { max.z },
                    ))
                };
                for (a, b) in [(0, 1), (2, 3), (4, 5), (6, 7), (0, 2), (1, 3), (4, 6), (5, 7), (0, 4), (1, 5), (2, 6), (3, 7)] {
                    out.push(Line { a: c(a), b: c(b), color: SELECTION_COLOR });
                }
            }
        }
    }
    out.extend_from_slice(gizmo);
    out
}

/// Draws lines over a picture, without depth (the editor's things are always seen).
pub fn draw_overlay(frame: &mut Frame, camera: &OrbitCamera, lines: &[Line], gizmo_from: usize) {
    let mut canvas = Canvas::new(frame, camera, false);
    for (i, l) in lines.iter().enumerate() {
        canvas.line(l.a, l.b, l.color, if i >= gizmo_from { 2 } else { 1 });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pixel(f: &Frame, x: u32, y: u32) -> [u8; 4] {
        let i = ((y * f.width + x) * 4) as usize;
        [f.pixels[i], f.pixels[i + 1], f.pixels[i + 2], f.pixels[i + 3]]
    }

    #[test]
    fn draws_the_cube_in_the_middle() {
        let mut scene = Scene::default();
        let cube = scene.add("Cube", Kind::mesh(Shape::Cube), None);
        if let Some(Kind::Mesh { color, .. }) = scene.get_mut(cube).map(|e| &mut e.kind) {
            *color = [1.0, 0.0, 0.0, 1.0];
        }
        let camera = OrbitCamera { target: Vec3::ZERO, ..Default::default() };
        let r = SoftwareRenderer::new();
        let frame = r.draw(&RenderInput { scene: &scene, selection: None, camera: &camera, show_grid: true, width: 160, height: 100 });
        let middle = pixel(&frame, 80, 50);
        assert!(middle[0] > 40 && middle[1] < 20 && middle[2] < 20, "{:?}", middle);
        let corner = pixel(&frame, 2, 2);
        assert!(corner[0] < 60, "{:?}", corner);
    }

    #[test]
    fn every_shape_has_triangles_inside_its_box() {
        for shape in Shape::ALL {
            let (min, max) = Kind::mesh(shape).local_bounds();
            let tris = triangles(shape);
            assert!(!tris.is_empty());
            for t in tris {
                for p in t {
                    assert!(p.x >= min.x - 1e-3 && p.y >= min.y - 2e-2 && p.z >= min.z - 1e-3, "{:?} {:?}", shape, p);
                    assert!(p.x <= max.x + 1e-3 && p.y <= max.y + 2e-2 && p.z <= max.z + 1e-3, "{:?} {:?}", shape, p);
                }
            }
        }
    }
}
