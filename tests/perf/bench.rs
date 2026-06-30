// Same two ops as the C/Ada/Java/Python benches. Safe idiomatic Rust: indexing
// is bounds-checked at run time (the optimizer elides most in these loops), the
// slice compare is a memcmp. Build: rustc -O bench.rs -o bench_rust
use std::env;
use std::fs;
use std::time::Instant;

fn count_lines(s: &[u8], pat: &[u8]) -> usize {
    let (mut hits, mut line) = (0usize, false);
    let (pl, p0, n) = (pat.len(), pat[0], s.len());
    let mut i = 0;
    while i < n {
        let b = s[i];
        if b == b'\n' {
            if line { hits += 1; }
            line = false;
        } else if !line && b == p0 && i + pl <= n && &s[i..i + pl] == pat {
            line = true;
        }
        i += 1;
    }
    if line { hits += 1; }
    hits
}

fn read_postings(path: &str) -> Vec<i64> {
    let d = fs::read(path).unwrap();
    let mut out = Vec::new();
    let (mut cur, mut any) = (0i64, false);
    for &b in &d {
        if b == b'\n' {
            if any { out.push(cur); }
            cur = 0; any = false;
        } else if b.is_ascii_digit() {
            cur = cur * 10 + (b - b'0') as i64;
            any = true;
        }
    }
    out // idx posting lists are already sorted ascending
}

fn intersect(a: &[i64], b: &[i64]) -> usize {
    let (mut i, mut j, mut c) = (0usize, 0usize, 0usize);
    while i < a.len() && j < b.len() {
        let (x, y) = (a[i], b[j]);
        if x == y { c += 1; i += 1; j += 1; }
        else if x < y { i += 1; }
        else { j += 1; }
    }
    c
}

fn main() {
    let a: Vec<String> = env::args().collect();
    let k = 7;
    if a[1] == "find" {
        let s = fs::read(&a[2]).unwrap();
        let pat = a[3].as_bytes();
        for it in 0..k {
            let t = Instant::now();
            let h = count_lines(&s, pat);
            println!("find  iter{}  {:5.0} ms  hits={}", it, t.elapsed().as_secs_f64() * 1e3, h);
        }
    } else {
        let (pa, pb) = (read_postings(&a[2]), read_postings(&a[3]));
        for it in 0..k {
            let t = Instant::now();
            let c = intersect(&pa, &pb);
            println!("and   iter{}  {:6.1} ms  common={} |A|={} |B|={}",
                     it, t.elapsed().as_secs_f64() * 1e3, c, pa.len(), pb.len());
        }
    }
}
