//! Error types for Clay code operations

/// Error type for Clay code operations
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ClayError {
    /// Invalid code parameters (k, m, d)
    InvalidParameters(String),
    /// Not enough helper nodes for repair
    InsufficientHelpers { needed: usize, provided: usize },
    /// Chunk size doesn't match expected sub-chunk alignment
    InvalidChunkSize { expected: usize, actual: usize },
    /// Helper provided insufficient data
    InsufficientHelperData { helper: usize, expected: usize, actual: usize },
    /// Chunks have inconsistent sizes
    InconsistentChunkSizes { first_size: usize, mismatched_idx: usize, mismatched_size: usize },
    /// Too many erasures to recover (max is m)
    TooManyErasures { max: usize, actual: usize },
    /// RS reconstruction failed
    ReconstructionFailed(String),
    /// Missing required y-section helper for repair
    MissingYSectionHelper { lost_node: usize, missing_helper: usize },
    /// Arithmetic overflow in parameter calculation
    Overflow(String),
}

impl std::fmt::Display for ClayError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ClayError::InvalidParameters(msg) => write!(f, "Invalid parameters: {}", msg),
            ClayError::InsufficientHelpers { needed, provided } => {
                write!(f, "Insufficient helpers: need {}, got {}", needed, provided)
            }
            ClayError::InvalidChunkSize { expected, actual } => {
                write!(f, "Invalid chunk size: expected divisible by {}, got {}", expected, actual)
            }
            ClayError::InsufficientHelperData { helper, expected, actual } => {
                write!(f, "Helper {} provided {} bytes, expected {}", helper, actual, expected)
            }
            ClayError::InconsistentChunkSizes { first_size, mismatched_idx, mismatched_size } => {
                write!(f, "Chunk {} has size {} but expected {} (same as first chunk)",
                       mismatched_idx, mismatched_size, first_size)
            }
            ClayError::TooManyErasures { max, actual } => {
                write!(f, "Too many erasures: max {} supported, got {}", max, actual)
            }
            ClayError::ReconstructionFailed(msg) => write!(f, "RS reconstruction failed: {}", msg),
            ClayError::MissingYSectionHelper { lost_node, missing_helper } => {
                write!(f, "Missing required y-section helper {} for repairing node {}",
                       missing_helper, lost_node)
            }
            ClayError::Overflow(msg) => write!(f, "Arithmetic overflow: {}", msg),
        }
    }
}

impl std::error::Error for ClayError {}
