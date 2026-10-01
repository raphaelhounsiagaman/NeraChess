# NeraChess — engine strength improvement backlog

Analysis of `main` @ `322a50a` (2026-08-10). Baseline measured on Apple M3, 8 logical
CPUs, Release build, 1 thread, 64 MiB hash, book disabled.

## Baseline measurements

`--search-bench` (fixed depth 6): 1.34M–3.00M nps.

Fixed 3 s per position via UCI (`scripts` probe, 8 positions):

| Position | depth | nodes | nps |
| --- | ---: | ---: | ---: |
| startpos | 15 | 5.28M | 2.03M |
| kiwipete | 12 | 2.41M | 1.32M |
| endgame (8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8) | 25 | 7.57M | 3.67M |
| pos4 | 17 | 4.61M | 1.77M |
| pos5 | 15 | 1.97M | 1.30M |
| sicilian | 15 | 3.55M | 1.51M |
| closed | 15 | 4.79M | 1.63M |
| kingatk | 13 | 2.35M | 1.45M |
| **total** | **127** | 32.5M | |

**The headline problem:** ~1.5–2.0M nps is a respectable node rate, but reaching only
depth 13–15 in a middlegame at 3 s is 4–6 plies shallower than engines with the same
node rate. The tree is not selective enough — nodes are being spent on moves that
should have been reduced or pruned. This is a search-shape problem, not a speed problem.

**Note (added after #49):** the node and nps figures above were measured before #49's
fix, which stopped double-counting every position handed from the main search to
quiescence. Reported node counts before that fix were 12–25% higher than the number of
distinct positions actually visited, with the exact inflation depending on tree shape
(measured at +24.5% on the nine-position sweep in #49). The absolute rates above
overstate the true node rate; the diagnosis that motivated this backlog — depth 4–6
plies shallower than expected at a comparable node rate — is a ratio between depth and
rate and is not itself invalidated by the correction, but the node-rate side of that
ratio was smaller than stated.

---

## Ranked improvements

### 1. Search selectivity overhaul — *biggest single lever*

`NeraChessSearch/src/SearchEngine.cpp` `PrincipalVariationSearch()`.

Everything in the selective layer is either capped at trivial depths or absent:

- **Reverse futility / static null move** is limited to `depth <= 2`
  (`SearchEngine.cpp:409`, margin `120 * depth`). Standard range is depth ≤ 6–8.
- **Futility pruning** is limited to `depth <= 2` (`SearchEngine.cpp:410`) and is
  evaluated *after* `MakeMove`, so the expensive part is paid before the decision.
- **Late move pruning / move-count pruning is entirely absent.** Nothing skips quiet
  moves purely because they are late at low depth.
- **SEE pruning in the main search is absent.** SEE is only used in qsearch
  (`SearchEngine.cpp:634`) and in move ordering. Losing captures and losing quiets are
  searched at full width.
- **History pruning is absent** — history scores never influence whether a move is
  skipped.
- **LMR is very weak** (`SearchEngine.cpp:860-870`): `1 + (depth>=6) + (moveIndex>=8) - pvNode`,
  so maximum reduction is ~3 plies and only 1–2 in most nodes. Modern log-based tables
  reach 4–7 plies for late quiets at high depth. It also ignores history score,
  improving, and cutnode.
- **No "improving" heuristic and no static-eval stack.** Every margin is depth-only;
  there is no notion of whether the position is getting better for the side to move.
- **No Internal Iterative Reduction** for nodes with no TT move.
- **No razoring, no ProbCut, no multi-cut.**

Expected: the largest available gain. Depth 15 → 19–20 at the same time budget.

### 2. Extensions — *completely missing*

`grep` for `depth + 1` across `NeraChessSearch/src` returns only an unrelated SEE loop.
The search never extends. Missing, in rough order of value:

- **Check extensions** (extend when the move gives check, gated by SEE or depth).
- **Singular extensions** (needs an `excludedMove` parameter threaded through
  `PrincipalVariationSearch` and a TT-key adjustment).
- Recapture / passed-pawn-push extensions.

Without extensions, forced tactical lines terminate at the same depth as quiet ones,
which is also why LMR cannot safely be made aggressive today.

### 3. Continuation history (follow-up / counter-move history)

`SearchEngine.h:128-129` has only a butterfly table `m_History[side][from][to]` and a
1-slot `m_CounterMoves[piece][to]`. There is no piece-to keyed continuation history at
1 and 2 plies back, and no capture history. Continuation history is one of the highest
value-per-line heuristics in modern engines: it improves ordering everywhere and gives
LMR/pruning a much better signal than a butterfly table.

### 4. Two-fold repetition detection inside the search

`ChessBoard::GetGameOver()` (`ChessBoard.cpp:648`) declares a draw only at
`GetRepetitionCount(...) >= 3`. Inside a search tree the standard is to score the
*first* repetition of a position as a draw. As written, NeraChess must find a genuine
three-fold before it sees a perpetual, so it can walk into perpetual check, miss a
saving perpetual, and waste nodes re-searching repeated positions. Needs a search-side
"repetition since root or twofold in history" test rather than a change to game rules.

### 5. Move-generation and per-node allocation overhead (speed)

- `ChessBoard::GetLegalMoves()` (`ChessBoard.cpp:605`) returns `MoveList<218>` **by
  value** — an ~880-byte copy at every node that calls it.
- `MoveList` value-initializes its 218-entry array (`MoveList.h:72`), so every
  construction memsets ~872 bytes.
- `SearchEngine::SortMoves()` zero-initializes `std::array<int32_t, 218> scores{}`
  (`SearchEngine.cpp:678`) at every node — another ~872-byte memset.
- `PrincipalVariationSearch` zero-initializes `std::array<Move, 218> quietMovesSearched{}`
  (`SearchEngine.cpp:449`) per node.
- `ChessBoard::MakeMove` pushes to `std::vector<Move> m_MovesPlayed` and
  `RepetitionTable`'s `std::vector<uint64_t>` per node (heap-backed).
- Quiescence generates **all** legal moves and then filters to captures
  (`SearchEngine.cpp:616-620`); there is no captures-only generation mode.
- Move ordering fully sorts every move list; staged/lazy selection would avoid scoring
  moves after a cutoff.

Together these are plausibly worth 1.5–2× nps.

### 6. Evaluation quality

`NeraChessSearch/src/Evaluation.cpp` is PeSTO piece-square tables plus a thin set of
hand-tuned terms. Missing or crude:

- King safety is a flat `-6 per attacked king-ring square` (`Evaluation.cpp:385`);
  no attacker-count/attack-weight table, no safe-check detection, no king zone beyond
  the 8 adjacent squares.
- No threat terms (hanging pieces, pawn pushes attacking pieces, minor-behind-pawn).
- No knight/bishop outposts, no rook-on-7th, no trapped-bishop/rook detection.
- Pawn structure has isolated/doubled/passed/supported only — no backward pawns, no
  connected-pawn ramp, no passed-pawn blocker/king-distance/rook-behind terms.
- No pawn hash table; `IsPassedPawn()` (`Evaluation.cpp:191`) is a nested loop per pawn
  per evaluation.
- No endgame scaling (opposite-coloured bishops, single-minor draws, wrong-rook-pawn),
  no 50-move-counter score damping.
- No specialized mate-with-KRK/KBNK drive-to-corner knowledge.

### 7. Evaluation tuning

`README.md` already lists this as a known limitation. Texel/Adam tuning of the PSQTs
and term weights against a labelled position set is normally worth a lot, but it needs
a self-generated dataset (this repo has no game database), so it is a project rather
than a patch.

### 8. NNUE evaluation

The single largest theoretical jump (several hundred Elo) but out of scope for one
change: it needs data generation, a trainer, an incremental accumulator in `ChessBoard`,
and a net file with a compatible license.

### 9. Time management

`NeraChessSearch/src/TimeManagement.cpp` divides remaining time by a fixed
move estimate. Missing:

- Best-move stability scaling (spend less when the root move has not changed for
  several iterations, more when it just changed).
- Score-drop / fail-low extension of the soft limit.
- Node-fraction-based effort redistribution across root moves.
- The soft-time check only happens *between* iterations (`SearchEngine.cpp:271`), so a
  long final iteration always overshoots to the hard limit.

### 10. Transposition table

- `TTEntry` (`TranspositionTable.h:21`) stores no static eval, so nothing can be
  cached for the pruning layer.
- Depth is `int8_t` and qsearch stores at depth 0, which mixes qsearch and depth-0
  main-search entries.
- No TT prefetch after choosing a move.
- Aspiration re-searches call `SearchRoot` from scratch rather than keeping root move
  ordering from the previous iteration.

### 11. Root/aspiration handling

- The window starts at ±25 and doubles symmetrically (`SearchEngine.cpp:229-248`);
  widening only on the failing side is standard and cheaper.
- Root moves are re-sorted from the TT each iteration rather than being kept in
  previous-iteration score order.
- No root move-count-based reduction and no per-root-move node accounting.

### 12. Syzygy endgame tablebases

Absent. Worth real Elo in long time controls and would repair endgame technique, but it
is an external dependency and a large amount of probing code.

---

## Chosen for implementation

**Item 1 — search selectivity overhaul**, on branch `search-selectivity`.

Rationale: the measured symptom (depth 13–15 at 3 s with 1.5–2 M nps) points directly
at it, it is entirely internal to `SearchEngine.cpp`, it needs no new data, no external
dependency, and no rules changes, and each sub-part is independently measurable with
self-play.

### What shipped

Depth-scaled reverse futility and futility pruning, late-move-count pruning, a
logarithmic LMR table adjusted by history/improving/cut-node/killer state, internal
iterative reduction, and a static-evaluation stack behind an `improving` signal.
Pruning decisions moved ahead of `MakeMove`, which needed a new
`ChessBoard::GivesCheck` so that checking moves could stay exempt.

Item 5 was partly addressed along the way: static exchange evaluation was walking rays
instead of using the magic tables it was built from. Switching it leaves every search
result identical and raises the node rate 6–9%.

Depth summed over the eight probe positions went from **127 to 141**.

### Measured, and rejected

Both were tried on top of the shipped layer and removed. They are recorded here so they
are not re-attempted blindly — each may still be worth revisiting under the conditions
noted.

| Change | Result (600 games, 3 s + 0.03 s) | Why |
| --- | --- | --- |
| Blanket check extensions, capped at `ply < 2 * rootDepth` | −11.6 Elo, 95% CI [−34.9, +12.2] | Cost ~1.4 plies of depth. The pruning exemption for checking moves already captures most of the benefit. A tighter gate (safe checks only, or PV nodes only) is untested. |
| SEE pruning of losing quiet moves, depth ≤ 7 | −22.6 Elo, 95% CI [−43.7, −1.7] | `StaticExchangeEvaluation` copies twelve bitboards per call and was measured before the magic-table fix. Worth retrying now that SEE is cheaper, and cheaper still if the sort's SEE results were reused instead of recomputed. |

### Natural follow-ups

Items 2 (extensions, with a tighter gate than the one rejected above), 3 (continuation
history), and 4 (search-side two-fold repetition) are the next candidates, in that
order. Item 5's remaining per-node copies were since measured at a bit-identical tree and
are *not* worth taking — see the table below.

---

## Strength-test results, September 2026

Measured with `.github/workflows/strength-test.yml` at 10+0.1 (see
`docs/Strength-Testing.md`), each against the `main` of its day. Recorded so that the
rejected ones are not re-attempted blindly; the linked issue holds the full measurement.

Two lessons from this batch. A fixed-length 1,000-game run is a screen, not a verdict:
#57's first screen read +4.9 Elo before its sequential test accepted it at +26.8. And a
smaller tree at fixed depth predicts little on its own — #54 cut it by up to 23% and #60
by 18%, and both still came back neutral or worse. What separated them was the gain at
fixed time: a few tenths of a ply for the rejected changes, below what an `elo1=5`
sequential test resolves, against +0.93 ply at 1 s for #57.

### Accepted

| Change | Issue / PR | Result |
| --- | --- | --- |
| History malus equal to the bonus on quiet beta cutoffs (was half) | #57 / #63 | +26.8 Elo [+14.7, +39.0], accepted after 1,000 games (LLR +3.20) |
| SEE pruning of losing captures: skip when `SEE < -50 * depth`, `depth <= 6`, non-PV, not giving check | #48 / #65 | +9.6 Elo [+4.4, +14.7], accepted after 6,000 games (LLR +5.11) |

### Rejected

| Change | Issue | Result | Why, and what is still untested |
| --- | --- | --- | --- |
| Captures-only move generation in quiescence | #35 (a) | −4.9 Elo [−17.6, +7.8], 1,000 games | Only −2% of search instructions left once (b) and (c) shipped, and it gives up exact stalemate detection at the quiescence horizon. If it returns, it returns as the capture stage of a staged picker (#43). |
| Wider aspiration window, ±25 → ±45 cp | #54 | +2.2 Elo [−1.6, +6.0] over 12,000 games, no verdict; a 1,000-game rerun on a later `main` read −6.3 [−18.6, +6.1] | The window width is worth about 2 Elo; an adaptive window tunes the same lever. Item 11's "widen only the failing side" measured +1.6% *more* nodes at depth 13 — do not take it on faith. |
| Singular-search multi-cut, without the extension | #60 | −5.2 Elo [−18.6, +8.1], 1,000 games; the issue's own fixed-node screen read −1.5 [−21.9, +18.8] over 450 games | Both halves negative. The singular *extension* was never played: +34% to +143% nodes at depth 13 and 17 fewer summed plies at 2 s. A retry should not trust null-move TT entries (#62). |
| Skip quiescence TT stores that understate a proven delta bound | #61 (B) | −4.9 Elo [−17.6, +7.8], 1,000 games | Variant (A), restoring the bound into `bestScore`, cost ~3 summed plies at 1 s and was not played. Removing delta pruning outright (−4.8% nodes at depth 11) was not played either. |
| Store null-move TT cutoffs at the depth actually searched | #62 (b) | +2.8 Elo [−9.7, +15.2], 1,000 games | +6.6% nodes at depth 16 for soundness alone. The tag-only variant (a) is a guard for #55 or a singular retry, not an Elo change. |
| Late move reductions for captures | #48 (2) | 0.0 Elo [−12.4, +12.4], 1,000 games | Exactly flat; the SEE-pruning half of the same issue was accepted instead. |
| Search the TT move before scoring and sorting the rest | #43, stage 1 | −6.9 Elo [−19.8, +5.9], 1,000 games | Suspected cause: the deferred scoring reads history the TT move's own subtree has already changed. The full staged picker is still open in #43. |
| Item 5's `MoveList` copy and value-initialization fixes | #54 (aside) | Bit-identical tree; faster in only 3 of 12 interleaved pairs (median ratio 1.009) | Out-of-order execution and `rep movsb` already hide the traffic. |
