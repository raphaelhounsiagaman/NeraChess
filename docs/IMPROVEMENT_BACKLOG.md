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
order. Item 5's remaining per-node copies are now the largest speed item left.
