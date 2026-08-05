# Engine strength calibration

## Result

At engine code commit `212e012`, NeraChess measured **2627 Stockfish 18
UCI-Elo-equivalent** at a `10+0.1` time control. A stratified paired-opening
bootstrap gives a **95% confidence interval of 2587--2664**.

This number is conditional on this test setup. It is not a FIDE rating, a
Chess.com or Lichess rating, or a claim that the engine will have the same Elo
at a different time control, on different hardware, or against a different
opponent pool.

## Focused tournament

The focused event used 100 games against each of three weakened Stockfish 18
anchors. W-D-L and score are from NeraChess's perspective.

| Opponent | W-D-L | Score | Rating implied by this opponent |
| --- | ---: | ---: | ---: |
| Stockfish 18, `UCI_Elo=2600` | 49-9-42 | 53.5% | 2624 |
| Stockfish 18, `UCI_Elo=2750` | 27-18-55 | 36.0% | 2650 |
| Stockfish 18, `UCI_Elo=2900` | 8-13-79 | 14.5% | 2592 |
| **Total** | **84-40-176** | **34.7%** | **2627 MLE** |

The result brackets cleanly across the three anchors rather than depending on
a single weakened setting. There were no crashes, illegal moves, or UCI
protocol failures. NeraChess and Stockfish each lost one game on time.

## Calibration ladder

A smaller 60-game ladder first located the useful rating range. Each opponent
played 12 games against NeraChess, with colors reversed on the same openings.

| Stockfish 18 `UCI_Elo` | NeraChess score |
| ---: | ---: |
| 2000 | 91.7% |
| 2300 | 91.7% |
| 2600 | 58.3% |
| 2900 | 20.8% |
| 3190 | 12.5% |

This preliminary ladder identified 2600--2900 as the focused test window. It
was not included in the final estimate.

## Test conditions

| Setting | Value |
| --- | --- |
| Date | 2026-08-04 |
| Host | Apple M3, 8 logical CPUs, macOS 26.6 arm64 |
| Tournament runner | Cute Chess CLI 1.5.1, Qt 6.11.1 |
| Reference engine | Stockfish 18 |
| Time control | 10 seconds plus 0.1 seconds per move |
| Threads | One per engine; NeraChess is single-threaded |
| Hash | 64 MiB per engine |
| Concurrency | 1 for calibration, 2 for the focused event |
| Openings | Paired games with colors reversed |
| Random seed | `20260804` |
| Time margin | 100 ms |
| Resign adjudication | 600 cp for 5 moves, confirmed by both engines |
| Draw adjudication | Within 20 cp for 10 moves after move 60 |
| Maximum length | 160 full moves |

The focused event used the official Stockfish books repository's
`2moves_v1.epd`; the calibration used `UHO_4060_v4.epd`. Their extracted-file
SHA-256 values were:

- `2moves_v1.epd`:
  `dc91f225bc93e7ec091095bf8264595da33d36b9d3ac97ddd2dd54bc3a094fa4`
- `UHO_4060_v4.epd`:
  `3f499996ff0b674a04f85f2634811d102dd53b5115841e8f11d18e1f550ba2ca`

## Statistical method

For candidate NeraChess rating `R` and opponent rating `r`, the expected score
was modeled with the conventional logistic Elo curve:

```text
E(R, r) = 1 / (1 + 10 ^ ((r - R) / 400))
```

The maximum-likelihood estimate is the `R` for which the expected aggregate
score against the three fixed anchors equals the observed 104/300 points.

For the interval, the 50 color-reversed opening pairs at each anchor were
resampled with replacement 200,000 times using seed `20260804`. The rating was
re-estimated for every sample. Resampling pairs, rather than pretending all 300
games were independent, preserves the correlation created by using the same
opening with both colors. The observed two-game pair-score distributions were:

| Anchor | 0 | 0.5 | 1 | 1.5 | 2 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 2600 | 8 | 2 | 24 | 7 | 9 |
| 2750 | 16 | 9 | 16 | 5 | 4 |
| 2900 | 31 | 11 | 6 | 2 | 0 |

The bootstrap median was 2627, its standard deviation was 19.6 Elo, and its
2.5th and 97.5th percentiles were 2587 and 2664.

## Reproduction outline

The focused event is reproducible with a release NeraChess UCI binary,
Stockfish 18, Cute Chess CLI 1.5.1, and the opening file identified above. The
substantive Cute Chess configuration was:

```sh
cutechess-cli \
  -engine name=NeraChess cmd=/path/to/NeraChessUCI proto=uci option.Hash=64 \
  -engine name=Stockfish-2600 cmd=/path/to/stockfish proto=uci option.Threads=1 option.Hash=64 option.UCI_LimitStrength=true option.UCI_Elo=2600 \
  -engine name=Stockfish-2750 cmd=/path/to/stockfish proto=uci option.Threads=1 option.Hash=64 option.UCI_LimitStrength=true option.UCI_Elo=2750 \
  -engine name=Stockfish-2900 cmd=/path/to/stockfish proto=uci option.Threads=1 option.Hash=64 option.UCI_LimitStrength=true option.UCI_Elo=2900 \
  -each tc=10+0.1 timemargin=100 \
  -tournament gauntlet -games 2 -rounds 50 -repeat \
  -openings file=/path/to/2moves_v1.epd format=epd order=random \
  -concurrency 2 -srand 20260804 \
  -resign movecount=5 score=600 twosided=true \
  -draw movenumber=60 movecount=10 score=20 \
  -maxmoves 160 -pgnout focused-2600-2750-2900.pgn
```

The generated PGN SHA-256 values were:

- Calibration ladder:
  `4ca0897d3992cf28bcc3bb607737f4b2deb2c3f30057c887bbe90e80de9b47cc`
- Focused tournament:
  `294ba6659a0a2584f530844d1e477df94039881ef679499bb9149a49a2d18e75`

## How to read this result

Stockfish's `UCI_Elo` is a convenient controlled reference scale, but weakened
Stockfish does not reproduce the error profile of a human player. The result is
best used as a regression baseline for future NeraChess versions. A slower
`3+2` validation, repeated on the same hardware and opening suite, would be the
most useful next measurement before comparing this value across time controls.
