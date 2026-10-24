use clap::Parser;

use gram_common::commands::Cli;
use gram_common::commands::Commands;
use gram_common::commands::add;
use gram_common::commands::build;
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
                Ok(_) => println!("Module dependency added successfully."),
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
