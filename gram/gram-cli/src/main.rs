use clap::Parser;

use gram_common::AddOutcome;
use gram_common::commands::Cli;
use gram_common::commands::Commands;
use gram_common::commands::add;
use gram_common::commands::build;
use gram_common::commands::deploy;
use gram_common::commands::generate;
use gram_common::commands::ide;
use gram_common::commands::init;
use gram_common::commands::remove;
use gram_common::commands::run;

fn main() {
    let cli = Cli::parse();

    match cli.command {
        Commands::Init(command) =>
            match init::project(command) {
                Ok(_) => println!("Project created successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        Commands::Build(command) => 
            match build::project(command) {
                Ok(_) => println!("Project built successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        Commands::Add(command) =>
            match add::module(command) {
                Ok(AddOutcome::DependencyAdded { module, dependency }) => {
                    let name = dependency.name();
                    println!("Added {name} to module {module}.");
                }
                Ok(AddOutcome::AlreadyPresent { module, dependency }) => {
                    let name = dependency.name();
                    eprintln!("warning: module {module} already depends on {name}, nothing to do");
                }
                Ok(AddOutcome::ModuleCreated { module }) =>
                    println!("Module {module} created."),
                Ok(AddOutcome::TargetsAdded { module, targets }) =>
                    println!("Module {module} now also targets {}.", targets.join(", ")),
                Ok(AddOutcome::NothingToDo { module }) =>
                    println!("Nothing to do: module {module} already exists with these targets."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        Commands::Remove(command) => 
            match remove::module(command) {
                Ok(_) => println!("Module dependency removed successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        Commands::Ide(command) =>
            match ide::project(command) {
                Ok(path) => println!("IDE project ready: {}", path.display()),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        Commands::Generate(command) =>
            match generate::project(command) {
                Ok(generations) => {
                    for generation in generations {
                        match generation.written.as_slice() {
                            [] => println!("{}: {} schema(s), {} pass(es), generated code unchanged", generation.module, generation.schemas, generation.passes),
                            written => println!(
                                "{}: {} schema(s), {} pass(es), wrote {}",
                                generation.module, generation.schemas, generation.passes,
                                written.iter().map(|path| path.display().to_string()).collect::<Vec<_>>().join(", ")
                            ),
                        }
                    }
                }
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        Commands::Deploy(command) =>
            match deploy::project(command) {
                Ok(code) => std::process::exit(code),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        Commands::Run(command) => 
            match run::module(command) {
                Ok(code) => std::process::exit(code),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
    }
}
