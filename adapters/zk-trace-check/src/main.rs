use curve25519_dalek::constants::RISTRETTO_BASEPOINT_POINT;
use curve25519_dalek::ristretto::{CompressedRistretto, RistrettoPoint};
use curve25519_dalek::scalar::Scalar;
use rand::rngs::OsRng;
use sha2::{Digest, Sha256, Sha512};
use std::env;
use std::fs;
use std::process::ExitCode;

const BACKEND: &str = "ristretto-pedersen-opening-v1";

fn read_bytes(path: &str) -> Result<Vec<u8>, String> {
    fs::read(path).map_err(|err| format!("failed to read {path}: {err}"))
}

fn sha256_bytes(input: &[u8]) -> [u8; 32] {
    let mut hasher = Sha256::new();
    hasher.update(input);
    hasher.finalize().into()
}

fn wide_hash(domain: &[u8], parts: &[&[u8]]) -> [u8; 64] {
    let mut hasher = Sha512::new();
    hasher.update(domain);
    for part in parts {
        hasher.update(part);
    }
    hasher.finalize().into()
}

fn scalar_from_domain(domain: &[u8], parts: &[&[u8]]) -> Scalar {
    Scalar::from_bytes_mod_order_wide(&wide_hash(domain, parts))
}

fn point_from_domain(domain: &[u8]) -> RistrettoPoint {
    RistrettoPoint::from_uniform_bytes(&wide_hash(b"etb-h-point", &[domain]))
}

fn hex_encode(bytes: &[u8]) -> String {
    let mut output = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        output.push_str(&format!("{byte:02x}"));
    }
    output
}

fn hex_decode(hex: &str) -> Result<Vec<u8>, String> {
    let bytes = hex.as_bytes();
    if bytes.len() % 2 != 0 {
        return Err("hex length must be even".to_string());
    }
    let mut out = Vec::with_capacity(bytes.len() / 2);
    for pair in bytes.chunks(2) {
        let hi = (pair[0] as char)
            .to_digit(16)
            .ok_or_else(|| "invalid hex".to_string())?;
        let lo = (pair[1] as char)
            .to_digit(16)
            .ok_or_else(|| "invalid hex".to_string())?;
        out.push(((hi << 4) | lo) as u8);
    }
    Ok(out)
}

fn parse_field(bundle: &str, key: &str) -> Option<String> {
    bundle
        .lines()
        .find_map(|line| line.strip_prefix(&format!("{key}=")).map(|v| v.to_string()))
}

fn statement_scalar(statement_hash: &[u8; 32]) -> Scalar {
    scalar_from_domain(b"etb-statement-scalar", &[statement_hash])
}

fn challenge_scalar(statement_hash: &[u8; 32], commitment: &[u8; 32], announcement: &[u8; 32]) -> Scalar {
    scalar_from_domain(
        b"etb-pedersen-opening",
        &[statement_hash, commitment, announcement],
    )
}

fn proof_bundle(certificate: &[u8]) -> String {
    let statement_hash = sha256_bytes(certificate);
    let message = statement_scalar(&statement_hash);
    let h = point_from_domain(b"etb-h-generator");
    let mut rng = OsRng;
    let opening = Scalar::random(&mut rng);
    let nonce = Scalar::random(&mut rng);
    let commitment = (RISTRETTO_BASEPOINT_POINT * message + h * opening).compress();
    let announcement = (h * nonce).compress();
    let challenge = challenge_scalar(
        &statement_hash,
        commitment.as_bytes(),
        announcement.as_bytes(),
    );
    let response = nonce + challenge * opening;

    format!(
        "backend={BACKEND}\nstatement_sha256={}\ncommitment={}\nannouncement={}\nresponse={}\n",
        hex_encode(&statement_hash),
        hex_encode(commitment.as_bytes()),
        hex_encode(announcement.as_bytes()),
        hex_encode(&response.to_bytes())
    )
}

fn parse_compressed_point(hex: &str, label: &str) -> Result<CompressedRistretto, String> {
    let bytes = hex_decode(hex)?;
    let array: [u8; 32] = bytes
        .try_into()
        .map_err(|_| format!("{label} must be 32 bytes"))?;
    Ok(CompressedRistretto(array))
}

fn parse_scalar(hex: &str, label: &str) -> Result<Scalar, String> {
    let bytes = hex_decode(hex)?;
    let array: [u8; 32] = bytes
        .try_into()
        .map_err(|_| format!("{label} must be 32 bytes"))?;
    Option::<Scalar>::from(Scalar::from_canonical_bytes(array))
        .ok_or_else(|| format!("invalid canonical scalar for {label}"))
}

fn verify_bundle(certificate: &[u8], bundle: &str) -> Result<(), String> {
    let backend = parse_field(bundle, "backend").ok_or_else(|| "missing backend".to_string())?;
    if backend != BACKEND {
        return Err(format!("unsupported backend {backend}"));
    }
    let statement_hex =
        parse_field(bundle, "statement_sha256").ok_or_else(|| "missing statement hash".to_string())?;
    let commitment_hex =
        parse_field(bundle, "commitment").ok_or_else(|| "missing commitment".to_string())?;
    let announcement_hex =
        parse_field(bundle, "announcement").ok_or_else(|| "missing announcement".to_string())?;
    let response_hex =
        parse_field(bundle, "response").ok_or_else(|| "missing response".to_string())?;

    let statement_hash = sha256_bytes(certificate);
    if statement_hex != hex_encode(&statement_hash) {
        return Err("statement hash mismatch".to_string());
    }

    let commitment = parse_compressed_point(&commitment_hex, "commitment")?
        .decompress()
        .ok_or_else(|| "invalid commitment point".to_string())?;
    let announcement = parse_compressed_point(&announcement_hex, "announcement")?
        .decompress()
        .ok_or_else(|| "invalid announcement point".to_string())?;
    let response = parse_scalar(&response_hex, "response")?;

    let message = statement_scalar(&statement_hash);
    let h = point_from_domain(b"etb-h-generator");
    let compressed_commitment = commitment.compress();
    let compressed_announcement = announcement.compress();
    let challenge = challenge_scalar(
        &statement_hash,
        compressed_commitment.as_bytes(),
        compressed_announcement.as_bytes(),
    );
    let lhs = h * response;
    let rhs = announcement + (commitment - RISTRETTO_BASEPOINT_POINT * message) * challenge;
    if lhs == rhs {
        Ok(())
    } else {
        Err("verification failed".to_string())
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        eprintln!("usage: zk-trace-check <segment-prove|fold|prove|verify> ...");
        return ExitCode::from(1);
    }
    match args[1].as_str() {
        "segment-prove" if args.len() == 3 => {
            let bytes = match read_bytes(&args[2]) {
                Ok(bytes) => bytes,
                Err(message) => {
                    eprintln!("{message}");
                    return ExitCode::from(2);
                }
            };
            println!("segment:{}", hex_encode(&sha256_bytes(&bytes)));
            ExitCode::SUCCESS
        }
        "fold" if args.len() == 4 => {
            let joined = format!("{}{}", args[2], args[3]);
            println!("fold:{}", hex_encode(&sha256_bytes(joined.as_bytes())));
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
            match verify_bundle(&certificate, &bundle) {
                Ok(()) => {
                    println!("ok");
                    ExitCode::SUCCESS
                }
                Err(message) => {
                    eprintln!("{message}");
                    ExitCode::from(2)
                }
            }
        }
        _ => {
            eprintln!("invalid command");
            ExitCode::from(1)
        }
    }
}
