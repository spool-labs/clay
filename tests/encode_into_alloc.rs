//! encode_into allocates none of the coded bytes, measured with a counting allocator

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};

use clay_codes::ClayCode;

struct Counting;

static LIVE: AtomicUsize = AtomicUsize::new(0);
static PEAK: AtomicUsize = AtomicUsize::new(0);

// SAFETY: every call forwards to the system allocator unchanged and only counts sizes
unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let live = LIVE.fetch_add(layout.size(), Ordering::SeqCst) + layout.size();
        PEAK.fetch_max(live, Ordering::SeqCst);
        unsafe { System.alloc(layout) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        LIVE.fetch_sub(layout.size(), Ordering::SeqCst);
        unsafe { System.dealloc(ptr, layout) }
    }
}

#[global_allocator]
static ALLOCATOR: Counting = Counting;

/// Peak bytes the call allocated past what was live when it started
fn peak_during(call: impl FnOnce()) -> usize {
    let base = LIVE.load(Ordering::SeqCst);
    PEAK.store(base, Ordering::SeqCst);
    call();
    PEAK.load(Ordering::SeqCst) - base
}

// one test, so no other test thread allocates while a call is measured
#[test]
fn encode_into_allocates_no_coded_bytes() {
    let clay = ClayCode::new(7, 13, 16).expect("tapenet shape");
    for size in [64 * 1024, 1024 * 1024, 8 * 1024 * 1024] {
        let data: Vec<u8> = (0..size).map(|i| (i % 251) as u8).collect();
        let chunk = clay.chunk_size(size);
        let coded = chunk * clay.n;

        let encoded = peak_during(|| {
            let chunks = clay.encode(&data);
            drop(chunks);
        });

        let mut chunks = vec![vec![0u8; chunk]; clay.n];
        let mut rows: Vec<&mut [u8]> = chunks.iter_mut().map(|chunk| chunk.as_mut_slice()).collect();
        let into = peak_during(|| clay.encode_into(&data, &mut rows).expect("encode_into"));

        eprintln!("{size} bytes: coded {coded}, encode peak {encoded}, encode_into peak {into}");
        assert!(encoded - into >= coded, "encode {encoded} against encode_into {into}, coded {coded}");
    }
}
