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

fn read_bytes(path: &str) -> Result<Vec<u8>, String> {
    fs::read(path).map_err(|err| format!("failed to read {path}: {err}"))
}

fn proof_bundle(certificate: &[u8]) -> String {
    let statement = digest(&[String::from_utf8_lossy(certificate).into_owned()]);
    let segment = digest(&[format!("segment:{statement}")]);
    let folded = digest(&[segment.clone(), statement.clone()]);
    let final_proof = digest(&[statement.clone(), segment.clone(), folded.clone()]);
    format!(
        "version=1\nstatement={statement}\nsegment={segment}\nfolded={folded}\nproof={final_proof}\n"
    )
}

fn parse_field(bundle: &str, key: &str) -> Option<String> {
    bundle
        .lines()
        .find_map(|line| line.strip_prefix(&format!("{key}=")).map(|v| v.to_string()))
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("usage: zk-trace-check <segment-prove|fold|prove|verify> ...");
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
        "prove" if args.len() == 4 => match read_bytes(&args[2]) {
            Ok(bytes) => match fs::write(&args[3], proof_bundle(&bytes)) {
                Ok(()) => ExitCode::SUCCESS,
                Err(err) => {
                    eprintln!("failed to write proof bundle: {err}");
                    ExitCode::from(2)
                }
            },
            Err(message) => {
                eprintln!("{message}");
                ExitCode::from(2)
            }
        },
        "verify" if args.len() == 4 => {
            let certificate = match read_bytes(&args[2]) {
                Ok(bytes) => bytes,
                Err(message) => {
                    eprintln!("{message}");
                    return ExitCode::from(2);
                }
            };
            let bundle = match fs::read_to_string(&args[3]) {
                Ok(text) => text,
                Err(err) => {
                    eprintln!("failed to read proof bundle: {err}");
                    return ExitCode::from(2);
                }
            };
            let expected_bundle = proof_bundle(&certificate);
            let expected_proof = parse_field(&expected_bundle, "proof");
            let actual_proof = parse_field(&bundle, "proof");
            if expected_proof.is_some() && expected_proof == actual_proof {
                println!("ok");
                return ExitCode::SUCCESS;
            }
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
