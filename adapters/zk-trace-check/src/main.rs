use std::collections::hash_map::DefaultHasher;
use std::env;
use std::fs;
use std::hash::{Hash, Hasher};
use std::process::ExitCode;

fn digest(parts: &[String]) -> String {
    let mut hasher = DefaultHasher::new();
    for part in parts {
        part.hash(&mut hasher);
    }
    format!("{:016x}", hasher.finish())
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("usage: zk-trace-check <segment-prove|fold|verify> ...");
        return ExitCode::from(1);
    }
    match args[1].as_str() {
        "segment-prove" if args.len() == 3 => {
            let input = fs::read_to_string(&args[2]).unwrap_or_default();
            println!("segment:{}", digest(&[input]));
            ExitCode::SUCCESS
        }
        "fold" if args.len() == 4 => {
            println!("fold:{}", digest(&[args[2].clone(), args[3].clone()]));
            ExitCode::SUCCESS
        }
        "verify" if args.len() == 4 => {
            let expected = digest(&[args[2].clone()]);
            if expected == args[3] {
                println!("ok");
                ExitCode::SUCCESS
            } else {
                eprintln!("verification failed");
                ExitCode::from(2)
            }
        }
        _ => {
            eprintln!("invalid command");
            ExitCode::from(1)
        }
    }
}
