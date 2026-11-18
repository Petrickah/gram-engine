//! Render passes described once in `.ron` files starting with `Pass(`, anywhere in a cargo module (the
//! game's `Content/`), and the code generated from them: one C++ header for the whole module
//! (`Passes.generated.hpp`, next to the `Cargo.toml`, beside `Components.generated.hpp`), one HLSL
//! header per pass (`<shader>.generated.hlsli`, next to the `.hlsl` that includes it) and one CMake file
//! compiling every pass's shader (`Shaders.generated.cmake`, next to the `Cargo.toml`).
//!
//! What a pass binds, and where (the same convention on both sides):
//! - set 0: the uniform at binding 0 (when it has fields), then each structured buffer;
//! - set 1: each sampled input (a combined image sampler), then each storage output.
//!
//! Every struct (the uniform, a buffer's element) is laid out by GPU rules with explicit padding, so the
//! C++ and HLSL sides are the same bytes whatever layout rules the shader compiler applies.

use std::fmt::Write;
use std::io;
use std::path::{Path, PathBuf};

use serde::Deserialize;

use crate::schema::{SchemaFile, pascal_case};

#[derive(Debug, Deserialize)]
struct Pass {
    name: String,
    stage: Stage,
    #[serde(default)] shader: Option<String>,   // calea .hlsl relativă la Content/; implicit <name>.hlsl lângă .ron
    #[serde(default)] uniform: Vec<Field>,
    #[serde(default)] buffers: Vec<Buffer>,
    #[serde(default)] inputs: Vec<Input>,
    #[serde(default)] outputs: Vec<Output>,
}

#[derive(Debug, Deserialize)] enum Stage { Raster, Compute }
#[derive(Debug, Deserialize)] struct Field { name: String, ty: GpuType }
/// `Mat4` is a 4x4 float matrix: `glm::mat4` and HLSL's `float4x4`, both column after column in memory
/// (glm's order, and dxc's default for `float4x4`), so a matrix built with glm reaches the shader as is.
#[derive(Debug, Deserialize)] enum GpuType { F32, I32, U32, Vec2, Vec3, Vec4, Mat4 }

/// A read-only array of structs (`StructuredBuffer<Element>`): `Buffer(name: "spheres", element:
/// "SphereData", fields: [...])`. The element struct is generated on both sides like the uniform.
#[derive(Debug, Deserialize)]
struct Buffer { name: String, element: String, fields: Vec<Field> }

/// An image the pass reads: `Sampled(name: "color")`, or `Sampled(name: "depth", ty: F32)` for one
/// channel (`Texture2D<float>`). Bound as a combined image sampler.
#[derive(Debug, Deserialize)]
enum Input { Sampled { name: String, #[serde(default = "texel_rgba")] ty: GpuType } }

fn texel_rgba() -> GpuType { GpuType::Vec4 }

/// An image the pass writes from a compute shader: `Storage(name: "result", format: Rgba32f)`.
#[derive(Debug, Deserialize)]
enum Output { Storage { name: String, format: StorageFormat } }

#[derive(Debug, Deserialize)]
enum StorageFormat { Rgba32f, Rgba16f, Rgba8, R32f }

impl StorageFormat {
    /// The `[[vk::image_format]]` name and the texel type of the `RWTexture2D`.
    fn hlsl(&self) -> (&'static str, &'static str) {
        match self {
            StorageFormat::Rgba32f => ("rgba32f", "float4"),
            StorageFormat::Rgba16f => ("rgba16f", "float4"),
            StorageFormat::Rgba8   => ("rgba8",   "float4"),
            StorageFormat::R32f    => ("r32f",    "float"),
        }
    }
}

impl GpuType {
    pub fn size_align(&self) -> (usize, usize) {
        match self {
            GpuType::F32  => (4, 4),
            GpuType::I32  => (4, 4),
            GpuType::U32  => (4, 4),
            GpuType::Vec2 => (8, 8),
            GpuType::Vec3 => (12, 16),
            GpuType::Vec4 => (16, 16),
            GpuType::Mat4 => (64, 16),
        }
    }

    pub fn hlsl_type(&self) -> &'static str {
        match self {
            GpuType::F32  => "float",
            GpuType::I32  => "int",
            GpuType::U32  => "uint",
            GpuType::Vec2 => "float2",
            GpuType::Vec3 => "float3",
            GpuType::Vec4 => "float4",
            GpuType::Mat4 => "float4x4",
        }
    }

    pub fn cpp_type(&self) -> &'static str {
        match self {
            GpuType::F32  => "float",
            GpuType::I32  => "int32_t",
            GpuType::U32  => "uint32_t",
            GpuType::Vec2 => "glm::vec2",
            GpuType::Vec3 => "glm::vec3",
            GpuType::Vec4 => "glm::vec4",
            GpuType::Mat4 => "glm::mat4",
        }
    }
}

enum Slot<'a> { Field(&'a Field, usize), Padding(usize) }

fn layout(fields: &[Field]) -> (Vec<Slot<'_>>, usize) {
    let mut slots = Vec::new();
    let mut offset = 0;

    for field in fields {
        let (size, align) = field.ty.size_align();
        let aligned = (offset + align - 1) / align * align;
        while offset < aligned {
            slots.push(Slot::Padding(offset));
            offset += 4;
        }
        slots.push(Slot::Field(field, offset));
        offset += size;
    }

    let size = (offset + 15) / 16 * 16;
    while offset < size { slots.push(Slot::Padding(offset)); offset += 4; }
    (slots, size)
}

/// The generated C++ header, next to the module's `Cargo.toml` (header-only).
pub const CPP_FILE: &str = "Passes.generated.hpp";
/// The generated CMake file, next to it: `gram_add_content_shaders(<target>)` compiles every pass's shader.
pub const CMAKE_FILE: &str = "Shaders.generated.cmake";

/// What `generate` returns: the C++ header's and the CMake file's content, and each pass's HLSL header
/// with its path relative to the module's directory (next to the `.hlsl` that includes it, so a plain
/// `#include "<Name>.generated.hlsli"` finds it).
pub struct GeneratedPasses {
    pub cpp: String,
    pub cmake: String,
    pub hlsl: Vec<(PathBuf, String)>,
}

/// One struct, C++ side: its fields and padding in layout order, `members` (constants) after them, then
/// the size and offset asserts. An empty field list (a pass without a uniform) makes a struct holding
/// only the constants, without asserts (an empty C++ struct has size 1).
fn emit_cpp_struct(out: &mut String, name: &str, fields: &[Field], members: &[String]) {
    let (slots, size) = layout(fields);
    writeln!(out).unwrap();
    writeln!(out, "struct {name}").unwrap();
    writeln!(out, "{{").unwrap();
    let mut pad = 0;
    for slot in &slots {
        match slot {
            Slot::Field(field, _) => writeln!(out, "    {} {};", field.ty.cpp_type(), pascal_case(&field.name)).unwrap(),
            Slot::Padding(_)      => { writeln!(out, "    uint32_t _Pad{pad};").unwrap(); pad += 1; }
        }
    }
    if !members.is_empty() {
        if !fields.is_empty() { writeln!(out).unwrap(); }
        for member in members { writeln!(out, "    {member}").unwrap(); }
    }
    writeln!(out, "}};").unwrap();

    if fields.is_empty() { return; }
    writeln!(out).unwrap();
    writeln!(out, "static_assert(sizeof({name}) == {size});").unwrap();
    for slot in &slots {
        if let Slot::Field(field, offset) = slot {
            writeln!(out, "static_assert(offsetof({name}, {}) == {offset});", pascal_case(&field.name)).unwrap();
        }
    }
}

/// One struct, HLSL side: the same slots, each with its offset as a comment (HLSL has no static_assert,
/// so that's where a layout is checked by eye when debugging a shader).
fn emit_hlsl_struct(out: &mut String, name: &str, fields: &[Field]) {
    let (slots, size) = layout(fields);
    writeln!(out).unwrap();
    writeln!(out, "struct {name}   // {size} bytes").unwrap();
    writeln!(out, "{{").unwrap();
    let mut pad = 0;
    for slot in &slots {
        match slot {
            Slot::Field(field, offset) => writeln!(out, "    {} {};   // {offset}", field.ty.hlsl_type(), pascal_case(&field.name)).unwrap(),
            Slot::Padding(offset)      => { writeln!(out, "    uint _Pad{pad};   // {offset}").unwrap(); pad += 1; }
        }
    }
    writeln!(out, "}};").unwrap();
}

/// Where each resource of a pass is bound: the uniform, the buffers (set 0), the inputs and the outputs
/// (set 1), by the convention in this module's documentation.
struct Bindings { uniform: Option<u32>, buffers: Vec<u32>, inputs: Vec<u32>, outputs: Vec<u32> }

impl Bindings {
    fn of(pass: &Pass) -> Self {
        let uniform = (!pass.uniform.is_empty()).then_some(0);
        let first_buffer = if uniform.is_some() { 1 } else { 0 };
        let buffers = (0..pass.buffers.len() as u32).map(|i| first_buffer + i).collect();
        let inputs  = (0..pass.inputs.len() as u32).collect();
        let outputs = (0..pass.outputs.len() as u32).map(|i| pass.inputs.len() as u32 + i).collect();
        Bindings { uniform, buffers, inputs, outputs }
    }
}

fn input_name(input: &Input) -> &str { match input { Input::Sampled { name, .. } => name } }
fn output_name(output: &Output) -> &str { match output { Output::Storage { name, .. } => name } }

fn emit_cpp(out: &mut String, pass: &Pass) {
    let bindings = Bindings::of(pass);

    for buffer in &pass.buffers {
        emit_cpp_struct(out, &buffer.element, &buffer.fields, &[]);
    }

    // The constants the C++ side of the pass needs: which shader, how many images in set 1, and where
    // each named resource is bound, so no binding number is written by hand.
    let mut members = vec![
        format!("static constexpr const char* Shader = \"{}\";", pass.name),
        format!("static constexpr uint32_t InputCount  = {};   // set 1, from binding 0: sampled images", pass.inputs.len()),
        format!("static constexpr uint32_t OutputCount = {};   // set 1, after the inputs: storage images", pass.outputs.len()),
    ];
    if let Some(binding) = bindings.uniform {
        members.push(format!("static constexpr uint32_t UniformBinding = {binding};   // set 0"));
    }
    for (buffer, binding) in pass.buffers.iter().zip(&bindings.buffers) {
        members.push(format!("static constexpr uint32_t {}Binding = {binding};   // set 0", pascal_case(&buffer.name)));
    }
    for (input, binding) in pass.inputs.iter().zip(&bindings.inputs) {
        members.push(format!("static constexpr uint32_t {}Binding = {binding};   // set 1", pascal_case(input_name(input))));
    }
    for (output, binding) in pass.outputs.iter().zip(&bindings.outputs) {
        members.push(format!("static constexpr uint32_t {}Binding = {binding};   // set 1", pascal_case(output_name(output))));
    }

    emit_cpp_struct(out, &format!("{}Params", pass.name), &pass.uniform, &members);
}

fn emit_hlsl(pass: &Pass, path: &Path) -> String {
    let bindings = Bindings::of(pass);
    let mut out = String::new();
    writeln!(out, "// Generated by gram from {}. Don't edit.", slash(path)).unwrap();
    writeln!(out, "#pragma once").unwrap();

    for buffer in &pass.buffers {
        emit_hlsl_struct(&mut out, &buffer.element, &buffer.fields);
    }
    if !pass.uniform.is_empty() {
        emit_hlsl_struct(&mut out, &format!("{}Params", pass.name), &pass.uniform);
    }

    writeln!(out).unwrap();
    if let Some(binding) = bindings.uniform {
        writeln!(out, "[[vk::binding({binding}, 0)]] ConstantBuffer<{}Params> Params;", pass.name).unwrap();
    }
    for (buffer, binding) in pass.buffers.iter().zip(&bindings.buffers) {
        writeln!(out, "[[vk::binding({binding}, 0)]] StructuredBuffer<{}> {};", buffer.element, pascal_case(&buffer.name)).unwrap();
    }
    for (input, binding) in pass.inputs.iter().zip(&bindings.inputs) {
        let Input::Sampled { name, ty } = input;
        let name = pascal_case(name);
        writeln!(out, "[[vk::binding({binding}, 1)]] [[vk::combinedImageSampler]] Texture2D<{}> {name};", ty.hlsl_type()).unwrap();
        writeln!(out, "[[vk::binding({binding}, 1)]] [[vk::combinedImageSampler]] SamplerState {name}Sampler;").unwrap();
    }
    for (output, binding) in pass.outputs.iter().zip(&bindings.outputs) {
        let Output::Storage { name, format } = output;
        let (format, texel) = format.hlsl();
        writeln!(out, "[[vk::binding({binding}, 1)]] [[vk::image_format(\"{format}\")]] RWTexture2D<{texel}> {};", pascal_case(name)).unwrap();
    }
    out
}

/// A path with `/` separators, for generated text that must read the same on every host.
fn slash(path: &Path) -> String {
    path.to_string_lossy().replace('\\', "/")
}

/// Turns every pass of one module into the generated files. A pass file's `path` is relative to the
/// module's directory; an error (a file that doesn't parse) names it.
pub fn generate(files: &[SchemaFile]) -> io::Result<GeneratedPasses> {
    let mut cpp = String::new();
    writeln!(cpp, "// Generated by gram from the .ron pass files in Content/. Don't edit.").unwrap();
    writeln!(cpp, "#pragma once").unwrap();
    writeln!(cpp).unwrap();
    for include in ["<cstddef>", "<cstdint>", "<glm/vec2.hpp>", "<glm/vec3.hpp>", "<glm/vec4.hpp>", "<glm/mat4x4.hpp>"] {
        writeln!(cpp, "#include {include}").unwrap();
    }

    // The function's body is filled below, one line per pass. CMAKE_CURRENT_LIST_DIR is this file's own
    // folder only while it's being included, so it's kept in a variable for the function to use later.
    let mut cmake = String::new();
    writeln!(cmake, "# Generated by gram from the .ron pass files in Content/. Don't edit.").unwrap();
    writeln!(cmake, "# include() it from a cmake module, then gram_add_content_shaders(<target>) compiles every").unwrap();
    writeln!(cmake, "# pass's shader for <target> (GramShaders.cmake's functions, so after add_subdirectory(GramEngine)).").unwrap();
    writeln!(cmake, "set(GRAM_CONTENT_DIR ${{CMAKE_CURRENT_LIST_DIR}})").unwrap();
    writeln!(cmake).unwrap();
    writeln!(cmake, "function(gram_add_content_shaders target)").unwrap();

    let mut hlsl = Vec::new();
    for file in files {
        let pass: Pass = ron::de::from_str(&file.source)
            .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, format!("{}: {err}", file.path.display())))?;

        // The shader: the one the .ron names, else <name>.hlsl next to it. Its generated header sits beside it.
        let shader = match &pass.shader {
            Some(path) => PathBuf::from(path),
            None       => file.path.with_file_name(format!("{}.hlsl", pass.name)),
        };

        emit_cpp(&mut cpp, &pass);
        hlsl.push((shader.with_extension("generated.hlsli"), emit_hlsl(&pass, &file.path)));

        let function = match pass.stage {
            Stage::Raster  => "gram_add_hlsl_raster_shader",
            Stage::Compute => "gram_add_hlsl_compute_shader",
        };
        writeln!(cmake, "    {function}(${{target}} ${{GRAM_CONTENT_DIR}}/{})", slash(&shader)).unwrap();
    }
    writeln!(cmake, "endfunction()").unwrap();

    Ok(GeneratedPasses { cpp, cmake, hlsl })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pass_file(source: &str) -> SchemaFile {
        SchemaFile { path: "shaders/Test.pass.ron".into(), source: source.into() }
    }

    #[test]
    fn dof_uniform_pads_to_32() {
        let field = |name: &str, ty| Field { name: name.into(), ty };
        let fields = vec![field("a", GpuType::Vec2), field("b", GpuType::F32), field("c", GpuType::F32),
                          field("d", GpuType::F32), field("e", GpuType::U32)];
        let (slots, size) = layout(&fields);

        let seen: Vec<(&str, usize)> = slots.iter().map(|slot| match slot {
            Slot::Field(f, offset) => (f.name.as_str(), *offset),
            Slot::Padding(offset)  => ("pad", *offset),
        }).collect();

        assert_eq!(seen, vec![("a", 0), ("b", 8), ("c", 12), ("d", 16), ("e", 20), ("pad", 24), ("pad", 28)]);
        assert_eq!(size, 32);
    }

    #[test]
    fn a_matrix_starts_a_register_and_fills_four() {
        let field = |name: &str, ty| Field { name: name.into(), ty };
        let fields = [field("scale", GpuType::F32), field("model", GpuType::Mat4)];
        let (slots, size) = layout(&fields);
        let offsets: Vec<usize> = slots.iter().filter_map(|slot| match slot { Slot::Field(_, o) => Some(*o), _ => None }).collect();

        assert_eq!(offsets, vec![0, 16]);
        assert_eq!(size, 80);
    }

    #[test]
    fn emits_both_sides() {
        let generated = generate(&[pass_file("Pass(name: \"Test\", stage: Raster, uniform: [Field(name: \"frame_count\", ty: U32)])")]).unwrap();

        assert!(generated.cpp.contains("uint32_t FrameCount;"));
        assert!(generated.cpp.contains("static_assert(sizeof(TestParams) == 16);"));
        let (path, hlsl) = &generated.hlsl[0];
        assert_eq!(path, &PathBuf::from("shaders/Test.generated.hlsli"));
        assert!(hlsl.contains("[[vk::binding(0, 0)]] ConstantBuffer<TestParams> Params;"));
        assert!(hlsl.contains("uint FrameCount;   // 0"));
        assert!(hlsl.contains("uint _Pad0;   // 4"));
    }

    #[test]
    fn inputs_are_set_1_in_order() {
        let generated = generate(&[pass_file(r#"Pass(name: "Test", stage: Raster,
            uniform: [Field(name: "enabled", ty: U32)],
            inputs: [Sampled(name: "color"), Sampled(name: "depth", ty: F32)])"#)]).unwrap();
        let (_, hlsl) = &generated.hlsl[0];

        assert!(hlsl.contains("[[vk::binding(0, 1)]] [[vk::combinedImageSampler]] Texture2D<float4> Color;"));
        assert!(hlsl.contains("[[vk::binding(0, 1)]] [[vk::combinedImageSampler]] SamplerState ColorSampler;"));
        assert!(hlsl.contains("[[vk::binding(1, 1)]] [[vk::combinedImageSampler]] Texture2D<float> Depth;"));
        assert!(generated.cpp.contains("static constexpr uint32_t InputCount  = 2;"));
        assert!(generated.cpp.contains("static constexpr uint32_t DepthBinding = 1;   // set 1"));
    }

    #[test]
    fn buffers_follow_the_uniform_and_outputs_follow_the_inputs() {
        let generated = generate(&[pass_file(r#"Pass(name: "Test", stage: Compute,
            uniform: [Field(name: "count", ty: U32)],
            buffers: [Buffer(name: "spheres", element: "SphereData", fields: [Field(name: "position", ty: Vec3), Field(name: "radius", ty: F32)])],
            outputs: [Storage(name: "result", format: Rgba32f), Storage(name: "depth", format: R32f)])"#)]).unwrap();
        let (_, hlsl) = &generated.hlsl[0];

        assert!(hlsl.contains("[[vk::binding(1, 0)]] StructuredBuffer<SphereData> Spheres;"));
        assert!(hlsl.contains("[[vk::binding(0, 1)]] [[vk::image_format(\"rgba32f\")]] RWTexture2D<float4> Result;"));
        assert!(hlsl.contains("[[vk::binding(1, 1)]] [[vk::image_format(\"r32f\")]] RWTexture2D<float> Depth;"));
        assert!(generated.cpp.contains("struct SphereData"));
        assert!(generated.cpp.contains("static_assert(sizeof(SphereData) == 16);"));
        assert!(generated.cpp.contains("static constexpr uint32_t SpheresBinding = 1;   // set 0"));
        assert!(generated.cmake.contains("gram_add_hlsl_compute_shader(${target} ${GRAM_CONTENT_DIR}/shaders/Test.hlsl)"));
    }

    #[test]
    fn a_pass_without_a_uniform_binds_its_buffers_from_0() {
        let generated = generate(&[pass_file(r#"Pass(name: "Test", stage: Compute,
            buffers: [Buffer(name: "items", element: "Item", fields: [Field(name: "value", ty: F32)])])"#)]).unwrap();
        let (_, hlsl) = &generated.hlsl[0];

        assert!(!hlsl.contains("ConstantBuffer"));
        assert!(hlsl.contains("[[vk::binding(0, 0)]] StructuredBuffer<Item> Items;"));
        assert!(!generated.cpp.contains("sizeof(TestParams)"));
    }
}
