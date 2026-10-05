# Rebased pipeline validation

This records a pinned integration checkpoint, not a moving-main estimate.
The baseline is main `575510a`; the candidate is `22d9d2c`, rebased onto that
baseline. Earlier reports retain their original revisions and scopes.
Later rebases do not retroactively update these measurements.

The rebase preserves main's Unicode column rules and hunk edit authority.
Include-order diagnostics now also request full source associations when
include sorting is enabled. Other whole-file checks retain minimal token
mapping. No parser restrictions, safety bypasses or cross-file caches are
introduced.

## Correctness

All 1,338 GoogleTest cases, the separate allocation check, repository
formatting and diff whitespace checks passed at this checkpoint. Goldens
were not regenerated. Formatter changes were reviewed and committed.

Separate full-map evidence streams have identical SHA256 for every corpus,
covering output, diagnostics, original positions, identities, source maps,
structural operations and protection. Every selected input succeeds, with
zero failures or skips. Diagnostic counts match:

| Corpus | Files | Semantic | Width |
| --- | ---: | ---: | ---: |
| Curated YTsaurus | 9,007 | 1,083 | 2,055 |
| LLVM/Clang | 3,917 | 31,478 | 7,847 |
| TensorFlow | 5,750 | 0 | 8,476 |
| Entire supported yt/ subtree | 10,330 | 1,259 | 3,854 |

The full yt/ selection overlaps the curated corpus and is never added to
its totals. It includes tests, examples, generated code and yt/cpp, applying
only the shipped configuration exclusion. Style approximation limitations
and all pinned input/configuration revisions remain as previously recorded.

## Runtime and hardware

Six complete closer-interleaved persistent-batch rounds use identical next
50-file batches, alternate order, pin one active worker to CPU 2 and retain
no trees or formatting results across files. All 24 corpus-round pairs favor
the candidate. The compiler, Release flags and environment are unchanged.
Three separate direct user-space hardware pairs have no multiplexing.

| Corpus | Main wall s | Candidate wall s | Reduction | IPB main -> candidate | CPB main -> candidate |
| --- | ---: | ---: | ---: | ---: | ---: |
| Curated YTsaurus | 14.2087 | 12.4829 | 12.15% | 1116.97 -> 988.50 | 605.31 -> 528.21 |
| LLVM/Clang | 17.8839 | 15.1988 | 15.01% | 1183.94 -> 1032.18 | 708.53 -> 599.68 |
| TensorFlow | 7.7250 | 6.7726 | 12.33% | 1024.05 -> 893.42 | 631.88 -> 531.46 |
| Primary corpus total | 39.8176 | 34.4543 | 13.47% | 1119.95 -> 981.57 | 653.60 -> 557.75 |
| Entire supported yt/ subtree | 13.3291 | 11.7442 | 11.89% | 1117.34 -> 977.70 | 579.95 -> 498.56 |

Wall totals sum per-corpus medians of CheckFile calls, excluding reads and
outer result destruction; they are not end-to-end CLI timings. Hardware
medians are byte-weighted for the primary total, count only user-space
CheckFile and exclude I/O, configuration and reporting. Cycles remain noisy.
Primary batch CPU medians sum to 41.3883 -> 35.8868 s, a 13.29% reduction;
full yt/ is separately 13.7221 -> 12.1200 s, an 11.68% reduction.

| Corpus | Allocation calls main -> candidate | Requested bytes main -> candidate | RSS KiB main -> candidate |
| --- | ---: | ---: | ---: |
| Curated YTsaurus | 871,159 -> 536,500 | 13,067,964,882 -> 12,166,367,866 | 60,720 -> 56,664 |
| LLVM/Clang | 472,652 -> 304,014 | 13,823,064,055 -> 12,449,043,015 | 226,404 -> 178,572 |
| TensorFlow | 344,990 -> 124,433 | 8,418,532,326 -> 7,845,431,854 | 37,356 -> 34,836 |
| Entire supported yt/ subtree | 994,708 -> 595,713 | 16,366,681,967 -> 14,871,412,695 | 296,148 -> 240,180 |

Requested bytes are cumulative, not live memory. RSS uses the unforced-map
hardware runs, not the correctness serialization processes.

## Evidence

Artifacts remain outside Git under /state/data/compact-pipeline/:
rebase-main-batch-* contains complete batch observations;
rebase-current-main/ contains archived baseline source/build, harnesses,
summary.json, hardware observations, full-evidence hashes, validation rows
and build/test/format logs. One combined command reached its shell deadline
after completing all batches and primary hardware runs; its partial full-yt
hardware pair is excluded. The full-yt hardware pairs were rerun completely.
No background process remains. Original worktrees and repros are unchanged.
