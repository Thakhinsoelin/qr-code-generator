//! The editor with the scene drawn on the CPU: no Bevy, no GPU for the scene.
//!
//!   raven-scene-editor-software [scene.scene]

fn main() {
    let open = std::env::args().nth(1).map(std::path::PathBuf::from);
    let code = editor_ui::run(Box::new(editor_ui::SoftwareRenderer::new()), open.as_deref());
    std::process::exit(code);
}
