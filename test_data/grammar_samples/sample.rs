// Rust sample
use std::collections::HashMap;

fn main() {
    let mut scores: HashMap<&str, i32> = HashMap::new();
    scores.insert("Alice", 100);
    scores.insert("Bob", 85);

    for (name, score) in &scores {
        println!("{}: {}", name, score);
    }

    let result: Result<i32, String> = Ok(42);
    if let Ok(value) = result {
        println!("Got: {}", value);
    }
}
