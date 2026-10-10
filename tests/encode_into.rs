//! encode_into writes the bytes the published encode returns, into the caller's buffers

use clay_codes::{ClayCode, ClayError};
use clay_published::ClayCode as PublishedClay;

/// (k, m, d) shapes, with and without shortened nodes
const SHAPES: [(usize, usize, usize); 5] = [(4, 2, 5), (4, 3, 5), (3, 2, 4), (10, 4, 13), (7, 13, 16)];

const SIZES: [usize; 7] = [0, 1, 777, 12_293, 65_549, 262_144, 1_048_577];

fn payload(size: usize) -> Vec<u8> {
    (0..size).map(|i| ((i * 131 + 7) % 251) as u8).collect()
}

fn encode_into_vecs(clay: &ClayCode, data: &[u8], fill: u8) -> Vec<Vec<u8>> {
    let size = clay.chunk_size(data.len());
    let mut chunks = vec![vec![fill; size]; clay.n];
    let mut rows: Vec<&mut [u8]> = chunks.iter_mut().map(|chunk| chunk.as_mut_slice()).collect();
    clay.encode_into(data, &mut rows).expect("encode_into");
    chunks
}

// every shape and size codes to the published 0.2.2 bytes
#[test]
fn matches_the_published_encode() {
    for (k, m, d) in SHAPES {
        let clay = ClayCode::new(k, m, d).expect("shape");
        let published = PublishedClay::new(k, m, d).expect("shape");
        for size in SIZES {
            let data = payload(size);
            let expected = published.encode(&data);
            assert_eq!(encode_into_vecs(&clay, &data, 0), expected, "({k},{m},{d}) at {size} bytes");
            assert_eq!(clay.encode(&data), expected, "({k},{m},{d}) at {size} bytes");
        }
    }
}

// buffers that held other bytes come out the same
#[test]
fn dirty_buffers_code_the_same() {
    let clay = ClayCode::new(7, 13, 16).expect("shape");
    let data = payload(65_549);
    assert_eq!(encode_into_vecs(&clay, &data, 0xA5), clay.encode(&data));
}

// the chunk size is the length encode gives every chunk
#[test]
fn chunk_size_matches_encode() {
    for (k, m, d) in SHAPES {
        let clay = ClayCode::new(k, m, d).expect("shape");
        for size in SIZES {
            assert_eq!(clay.chunk_size(size), clay.encode(&payload(size))[0].len());
        }
    }
}

// a wrong chunk count or size is refused before anything is written
#[test]
fn refuses_wrong_buffers() {
    let clay = ClayCode::new(4, 2, 5).expect("shape");
    let data = payload(777);
    let size = clay.chunk_size(data.len());

    let mut short = vec![vec![0u8; size]; clay.n - 1];
    let mut rows: Vec<&mut [u8]> = short.iter_mut().map(|chunk| chunk.as_mut_slice()).collect();
    assert!(matches!(clay.encode_into(&data, &mut rows), Err(ClayError::InvalidParameters(_))));

    let mut narrow = vec![vec![0u8; size - 1]; clay.n];
    let mut rows: Vec<&mut [u8]> = narrow.iter_mut().map(|chunk| chunk.as_mut_slice()).collect();
    assert!(matches!(
        clay.encode_into(&data, &mut rows),
        Err(ClayError::InvalidChunkSize { expected, actual }) if expected == size && actual == size - 1
    ));
}
