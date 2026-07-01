// merge.rs -- the ais k-way merge (intersection / union) reimplemented in Rust
// with a C ABI, as a proof of concept for a Rust engine behind embed.h.
//
// Mirrors c/merge.c's merge_run: smallest live head across N sorted streams,
// AND keeps an id present in every stream, OR keeps any, both dedup ascending.
// The tombstone suppression and the emit callback stay C-side for now; this is
// the pure compute core.
//
// no_std, no allocation, NO external crates (not even the Rust std library):
// the caller owns every buffer, exactly like ais's stack-and-stream C. Build:
//   rustc --edition 2021 -O -C panic=abort -C strip=symbols \
//         --crate-type staticlib merge.rs -o libaismerge.a
// then link libaismerge.a with cc like any C archive. No cargo, no vendoring.

#![no_std]

use core::slice;

// no_std needs a panic handler; -C panic=abort means it just aborts. The merge
// below has no panicking paths (every index is guarded), so this never fires.
#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

// The precompiled `core` in the sysroot references this unwind personality even
// under -C panic=abort. With abort it is never called; the stub just satisfies
// the linker so the .a drops into a C program cleanly.
#[no_mangle]
extern "C" fn rust_eh_personality() {}

// Must match the C struct byte-for-byte (see poc.c).
#[repr(C)]
pub struct AisStream {
    ids: *const i64, // sorted ascending, duplicate-free
    len: i64,
}

// A query intersects a handful of keys, so a small fixed cap keeps this
// allocation-free. The real engine streams post_stream heads with no cap.
const MAX_STREAMS: usize = 64;

// mode 0 = AND (intersection), 1 = OR (union). Writes up to out_cap survivors
// to out, ascending, and returns the true count (which may exceed out_cap, so
// the caller can detect truncation). Returns -1 on bad input.
#[no_mangle]
pub extern "C" fn ais_merge_ids(
    streams: *const AisStream,
    nstreams: i32,
    mode: i32,
    out: *mut i64,
    out_cap: i64,
) -> i64 {
    let n = nstreams as usize;
    if streams.is_null() || out.is_null() || nstreams < 0 || n > MAX_STREAMS || out_cap < 0 {
        return -1;
    }
    let sv = unsafe { slice::from_raw_parts(streams, n) };
    let outv = unsafe { slice::from_raw_parts_mut(out, out_cap as usize) };

    let mut cur = [0i64; MAX_STREAMS]; // per-stream cursor, on the stack, no heap
    let mut count: i64 = 0;
    let mut last: i64 = 0;
    let mut have_last = false;

    // current id of stream i, or None if exhausted
    let head = |cur: &[i64; MAX_STREAMS], i: usize| -> Option<i64> {
        let s = &sv[i];
        let c = cur[i];
        if c >= 0 && c < s.len && !s.ids.is_null() {
            Some(unsafe { *s.ids.offset(c as isize) })
        } else {
            None
        }
    };

    loop {
        // smallest live head across all streams
        let mut min: Option<i64> = None;
        for i in 0..n {
            if let Some(h) = head(&cur, i) {
                if min.map_or(true, |m| h < m) {
                    min = Some(h);
                }
            }
        }
        let min = match min {
            Some(m) => m,
            None => break, // every stream exhausted
        };

        let emit = !(have_last && min == last);
        let survive = if mode == 0 {
            (0..n).all(|i| head(&cur, i) == Some(min)) // AND: present everywhere
        } else {
            true // OR: present anywhere (min already is)
        };

        if survive && emit {
            if count < out_cap {
                outv[count as usize] = min;
            }
            count += 1;
            last = min;
            have_last = true;
        }
        // advance every stream sitting at the minimum
        for i in 0..n {
            if head(&cur, i) == Some(min) {
                cur[i] += 1;
            }
        }
    }
    count
}
