use std::io;

use crate::config::BuildConfig;
use crate::directory::Directory;

impl BuildConfig {
    // Merges into .vscode/settings.json rather than overwriting it, so any settings a
    // user (or an IDE extension, e.g. CMake Tools) added by hand survive every add/remove.
    pub(crate) fn write_vscode_settings(&self, project: &Directory) -> io::Result<()> {
        let manifests: Vec<&str> = self.modules.iter()
            .filter_map(|m| m.backend().ok()?.rust_analyzer_manifest(m))
            .collect();

        let existing = project.read_source(".vscode/settings.json")?;
        let mut settings: serde_json::Value = match &existing {
            Some(contents) => serde_json::from_str(contents).unwrap_or(serde_json::json!({})),
            None => serde_json::json!({}),
        };
        let settings_obj = settings.as_object_mut().expect("settings.json root must be an object");

        if manifests.is_empty() {
            settings_obj.remove("rust-analyzer.linkedProjects");
        } else {
            settings_obj.insert(
                "rust-analyzer.linkedProjects".to_string(),
                serde_json::Value::Array(manifests.iter().map(|p| serde_json::Value::String(p.to_string())).collect()),
            );
        }

        if settings_obj.is_empty() {
            return Ok(());
        }

        let pretty = serde_json::to_string_pretty(&settings)
            .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?;
        project.write_source(".vscode/settings.json", &format!("{pretty}\n"))
    }
}
