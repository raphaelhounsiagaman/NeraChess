import chess
import chess.pgn
import chess.engine
import random
import csv
import os

# --- CONFIG ---
pgn_file = "lichess_db_standard_rated_2019-01.pgn"  # your PGN file
output_csv = "chessData.csv"
num_positions_to_generate = 100000000000  # adjust as needed
stockfish_path = "stockfish-windows-x86-64-avx2.exe"  # adjust path to your Stockfish binary
sf_depth = 1  # shallow evaluation for speed

# --- Initialize Stockfish engine ---
engine = chess.engine.SimpleEngine.popen_uci(stockfish_path)

# --- Open PGN file ---
pgn = open(pgn_file)

# --- Prepare CSV ---
file_exists = os.path.exists(output_csv)
csv_file = open(output_csv, "a", newline="")
csv_writer = csv.writer(csv_file)

positions_generated = 0

while positions_generated < num_positions_to_generate:
    game = chess.pgn.read_game(pgn)
    if game is None:
        # End of PGN reached, rewind file
        pgn.seek(0)
        continue

    board = game.board()
    moves = list(game.mainline_moves())
    if len(moves) == 0:
        continue

    # Pick a random move index to get a random position from this game
    random_index = random.randint(0, len(moves) - 1)
    for move in moves[:random_index]:
        board.push(move)

    # Evaluate with Stockfish
    try:
        info = engine.analyse(board, chess.engine.Limit(depth=sf_depth))
        score = info["score"].white().score(mate_score=10000)
        if score is None:
            # Skip positions that are checkmate / stalemate
            continue
    except Exception as e:
        print(f"Error evaluating position: {e}")
        continue

    # Write FEN + eval to CSV
    csv_writer.writerow([board.fen(), score])

    positions_generated += 1
    if positions_generated % 1000 == 0:
        print(f"Generated {positions_generated} positions...")

engine.quit()
csv_file.close()
print(f"Done! Generated {positions_generated} positions in {output_csv}")
