use std::fs;
use std::io;
use std::path::Path;

use super::SchemaFile;

/// Every component schema under `dir`: a `.ron` file whose first item is `Component(`.
pub(crate) fn find(dir: &Path) -> io::Result<Vec<SchemaFile>> {
    find_items(dir, "Component(")
}

/// Every `.ron` file under `dir`, wherever it is, whose first item starts with `item` (`"Component("`,
/// `"Pass("`; comments and blank lines before it are allowed), so `Gram.ron` and other data files are
/// left alone. Build output (`target/`, `build*/`) and hidden directories are skipped. Sorted by path,
/// so the generated files don't change order from one run to the next.
pub(crate) fn find_items(dir: &Path, item: &str) -> io::Result<Vec<SchemaFile>> {
    let mut schemas = Vec::new();
    walk(dir, dir, item, &mut schemas)?;
    schemas.sort_by(|a, b| a.path.cmp(&b.path));
    Ok(schemas)
}

fn walk(root: &Path, dir: &Path, item: &str, schemas: &mut Vec<SchemaFile>) -> io::Result<()> {
    for entry in fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        let name = entry.file_name().to_string_lossy().into_owned();
        if entry.file_type()?.is_dir() {
            if name == "target" || name.starts_with("build") || name.starts_with('.') {
                continue;
            }
            walk(root, &path, item, schemas)?;
        } else if name.ends_with(".ron") {
            let source = fs::read_to_string(&path)?;
            if starts_with_item(&source, item) {
                let path = path.strip_prefix(root).unwrap_or(&path).to_path_buf();
                schemas.push(SchemaFile { path, source });
            }
        }
    }
    Ok(())
}

fn starts_with_item(source: &str, item: &str) -> bool {
    source.lines()
        .map(str::trim)
        .find(|line| !line.is_empty() && !line.starts_with("//"))
        .is_some_and(|line| line.starts_with(item))
}

/// Writes `content` to `path` only if it differs from what's there: an unchanged header must
/// keep its timestamp, or CMake recompiles everything that includes it on every build.
/// Returns whether the file was written.
pub(crate) fn write_if_changed(path: &Path, content: &str) -> io::Result<bool> {
    if fs::read_to_string(path).is_ok_and(|existing| existing == content) {
        return Ok(false);
    }
    fs::write(path, content)?;
    Ok(true)
}
