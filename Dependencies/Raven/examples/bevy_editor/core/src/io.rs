//! The scene file: plain text, one entity per block, easy to read and to diff.
//!
//! ```text
//! raven-bevy-scene 1
//! ambient 1 1 1 300
//! background 0.11 0.12 0.14
//!
//! entity 2 "Cube"
//! parent 1
//! position 0 0.5 0
//! rotation 0 45 0
//! scale 1 1 1
//! visible true
//! mesh cube 0.29 0.49 1 1 metallic 0 roughness 0.5
//! end
//! ```

use crate::math::Vec3;
use crate::scene::{Entity, Kind, Scene, Shape, Transform};

pub const MAGIC: &str = "raven-bevy-scene 1";

fn f(v: f32) -> String {
    // short when it can be: 1 rather than 1.0000000
    let s = format!("{}", v);
    if s.contains('e') {
        format!("{:.6}", v)
    } else {
        s
    }
}

fn quote(s: &str) -> String {
    let mut out = String::from("\"");
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

pub fn save(scene: &Scene) -> String {
    let mut out = String::new();
    out.push_str(MAGIC);
    out.push('\n');
    out.push_str(&format!("ambient {} {} {} {}\n", f(scene.ambient[0]), f(scene.ambient[1]), f(scene.ambient[2]), f(scene.ambient_brightness)));
    out.push_str(&format!("background {} {} {}\n", f(scene.background[0]), f(scene.background[1]), f(scene.background[2])));
    for e in &scene.entities {
        out.push('\n');
        out.push_str(&format!("entity {} {}\n", e.id, quote(&e.name)));
        if let Some(p) = e.parent {
            out.push_str(&format!("parent {}\n", p));
        }
        let t = &e.transform;
        out.push_str(&format!("position {} {} {}\n", f(t.position.x), f(t.position.y), f(t.position.z)));
        out.push_str(&format!("rotation {} {} {}\n", f(t.rotation.x), f(t.rotation.y), f(t.rotation.z)));
        out.push_str(&format!("scale {} {} {}\n", f(t.scale.x), f(t.scale.y), f(t.scale.z)));
        out.push_str(&format!("visible {}\n", e.visible));
        match &e.kind {
            Kind::Empty => out.push_str("empty\n"),
            Kind::Mesh { shape, color, metallic, roughness } => out.push_str(&format!(
                "mesh {} {} {} {} {} metallic {} roughness {}\n",
                shape.name(),
                f(color[0]),
                f(color[1]),
                f(color[2]),
                f(color[3]),
                f(*metallic),
                f(*roughness)
            )),
            Kind::PointLight { color, intensity, range, shadows } => out.push_str(&format!(
                "point_light {} {} {} intensity {} range {} shadows {}\n",
                f(color[0]),
                f(color[1]),
                f(color[2]),
                f(*intensity),
                f(*range),
                shadows
            )),
            Kind::DirectionalLight { color, illuminance, shadows } => out.push_str(&format!(
                "directional_light {} {} {} illuminance {} shadows {}\n",
                f(color[0]),
                f(color[1]),
                f(color[2]),
                f(*illuminance),
                shadows
            )),
            Kind::Camera { fov } => out.push_str(&format!("camera fov {}\n", f(*fov))),
        }
        out.push_str("end\n");
    }
    out
}

#[derive(Debug, PartialEq)]
pub struct LoadError {
    pub line: usize,
    pub message: String,
}

impl std::fmt::Display for LoadError {
    fn fmt(&self, fm: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(fm, "line {}: {}", self.line, self.message)
    }
}

/// Words of a line; a "quoted string" is one word.
fn words(line: &str) -> Result<Vec<String>, String> {
    let mut out = Vec::new();
    let mut chars = line.chars().peekable();
    while let Some(&c) = chars.peek() {
        if c.is_whitespace() {
            chars.next();
            continue;
        }
        if c == '"' {
            chars.next();
            let mut s = String::new();
            loop {
                match chars.next() {
                    Some('"') => break,
                    Some('\\') => match chars.next() {
                        Some('n') => s.push('\n'),
                        Some(other) => s.push(other),
                        None => return Err("unfinished string".into()),
                    },
                    Some(other) => s.push(other),
                    None => return Err("unfinished string".into()),
                }
            }
            out.push(s);
        } else {
            let mut s = String::new();
            while let Some(&c) = chars.peek() {
                if c.is_whitespace() {
                    break;
                }
                s.push(c);
                chars.next();
            }
            out.push(s);
        }
    }
    Ok(out)
}

struct Reader<'a> {
    w: &'a [String],
    at: usize,
}

impl<'a> Reader<'a> {
    fn num(&mut self) -> Result<f32, String> {
        let word = self.w.get(self.at).ok_or("a number is missing")?;
        self.at += 1;
        word.parse::<f32>().map_err(|_| format!("'{}' is not a number", word))
    }
    fn vec3(&mut self) -> Result<Vec3, String> {
        Ok(Vec3::new(self.num()?, self.num()?, self.num()?))
    }
    fn boolean(&mut self) -> Result<bool, String> {
        let word = self.w.get(self.at).ok_or("true or false is missing")?;
        self.at += 1;
        match word.as_str() {
            "true" | "1" | "yes" => Ok(true),
            "false" | "0" | "no" => Ok(false),
            other => Err(format!("'{}' is not true or false", other)),
        }
    }
    /// `name value` pairs after the fixed words, in any order.
    fn named(&mut self, name: &str) -> Option<usize> {
        let i = self.w.iter().skip(1).position(|x| x == name)? + 2;
        Some(i)
    }
    fn named_num(&mut self, name: &str, default: f32) -> Result<f32, String> {
        match self.named(name) {
            Some(i) => {
                self.at = i;
                self.num()
            }
            None => Ok(default),
        }
    }
    fn named_bool(&mut self, name: &str, default: bool) -> Result<bool, String> {
        match self.named(name) {
            Some(i) => {
                self.at = i;
                self.boolean()
            }
            None => Ok(default),
        }
    }
}

pub fn load(text: &str) -> Result<Scene, LoadError> {
    let mut scene = Scene { entities: Vec::new(), ..Default::default() };
    let mut current: Option<Entity> = None;
    let mut seen_magic = false;
    let mut max_id = 0;
    for (n, raw) in text.lines().enumerate() {
        let line_no = n + 1;
        let err = |message: String| LoadError { line: line_no, message };
        let line = raw.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if !seen_magic {
            if line != MAGIC {
                return Err(err("this is not a Raven Bevy scene".into()));
            }
            seen_magic = true;
            continue;
        }
        let w = words(line).map_err(err)?;
        let mut r = Reader { w: &w, at: 1 };
        let result: Result<(), String> = (|| {
            match (w[0].as_str(), current.as_mut()) {
                ("ambient", None) => {
                    let c = r.vec3()?;
                    scene.ambient = c.to_array();
                    scene.ambient_brightness = r.num()?;
                }
                ("background", None) => scene.background = r.vec3()?.to_array(),
                ("entity", None) => {
                    let id = r.num()? as u64;
                    if id == 0 || scene.contains(id) {
                        return Err(format!("entity {} is there twice", id));
                    }
                    let name = w.get(2).cloned().unwrap_or_else(|| "Entity".into());
                    max_id = max_id.max(id);
                    current = Some(Entity { id, name, parent: None, transform: Transform::default(), visible: true, kind: Kind::Empty });
                }
                ("parent", Some(e)) => e.parent = Some(r.num()? as u64),
                ("position", Some(e)) => e.transform.position = r.vec3()?,
                ("rotation", Some(e)) => e.transform.rotation = r.vec3()?,
                ("scale", Some(e)) => e.transform.scale = r.vec3()?,
                ("visible", Some(e)) => e.visible = r.boolean()?,
                ("empty", Some(e)) => e.kind = Kind::Empty,
                ("mesh", Some(e)) => {
                    let shape_name = w.get(1).ok_or("the shape is missing")?;
                    let shape = Shape::from_name(shape_name).ok_or(format!("unknown shape '{}'", shape_name))?;
                    r.at = 2;
                    let c = [r.num()?, r.num()?, r.num()?, r.num().unwrap_or(1.0)];
                    e.kind = Kind::Mesh { shape, color: c, metallic: r.named_num("metallic", 0.0)?, roughness: r.named_num("roughness", 0.5)? };
                }
                ("point_light", Some(e)) => {
                    let c = r.vec3()?.to_array();
                    e.kind = Kind::PointLight {
                        color: c,
                        intensity: r.named_num("intensity", 1_000_000.0)?,
                        range: r.named_num("range", 20.0)?,
                        shadows: r.named_bool("shadows", true)?,
                    };
                }
                ("directional_light", Some(e)) => {
                    let c = r.vec3()?.to_array();
                    e.kind = Kind::DirectionalLight { color: c, illuminance: r.named_num("illuminance", 10_000.0)?, shadows: r.named_bool("shadows", true)? };
                }
                ("camera", Some(e)) => e.kind = Kind::Camera { fov: r.named_num("fov", 45.0)? },
                ("end", Some(_)) => {
                    let e = current.take().unwrap();
                    scene.entities.push(e);
                }
                (word, Some(_)) => return Err(format!("'{}' has no meaning inside an entity", word)),
                (word, None) => return Err(format!("'{}' has no meaning outside an entity", word)),
            }
            Ok(())
        })();
        result.map_err(err)?;
    }
    if !seen_magic {
        return Err(LoadError { line: 1, message: "the file is empty".into() });
    }
    if current.is_some() {
        return Err(LoadError { line: text.lines().count(), message: "the last entity has no 'end'".into() });
    }
    // a parent that is not there, or a loop: the entity becomes a root
    let ids: Vec<u64> = scene.entities.iter().map(|e| e.id).collect();
    for i in 0..scene.entities.len() {
        if let Some(p) = scene.entities[i].parent {
            if !ids.contains(&p) || scene.is_ancestor(scene.entities[i].id, p) {
                scene.entities[i].parent = None;
            }
        }
    }
    // the order of the hierarchy (a child written before its parent moves after it)
    let mut ordered = Vec::with_capacity(scene.entities.len());
    let mut stack: Vec<u64> = scene.children(None).into_iter().rev().collect();
    while let Some(id) = stack.pop() {
        ordered.push(scene.get(id).unwrap().clone());
        stack.extend(scene.children(Some(id)).into_iter().rev());
    }
    scene.entities = ordered;
    scene.next_id = max_id + 1;
    Ok(scene)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip() {
        let mut scene = Scene::starter();
        let cube = scene.children(None)[1];
        let lamp = scene.add("Lamp \"warm\"", Kind::point_light(), Some(cube));
        scene.get_mut(lamp).unwrap().visible = false;
        let text = save(&scene);
        let back = load(&text).unwrap();
        assert_eq!(back, scene, "{}", text);
    }

    #[test]
    fn errors_and_repairs() {
        assert_eq!(load("hello").unwrap_err().line, 1);
        let e = load("raven-bevy-scene 1\nentity 1 \"A\"\nposition 1 x 3\nend\n").unwrap_err();
        assert_eq!(e.line, 3);
        assert!(e.message.contains("'x'"));
        // a child before its parent, a parent that does not exist
        let s = load("raven-bevy-scene 1\nentity 2 \"B\"\nparent 1\nend\nentity 1 \"A\"\nend\nentity 3 \"C\"\nparent 9\nend\n").unwrap();
        let order: Vec<_> = s.entities.iter().map(|e| e.id).collect();
        assert_eq!(order, vec![1, 2, 3]);
        assert_eq!(s.get(3).unwrap().parent, None);
        assert_eq!(s.next_id, 4);
    }
}
