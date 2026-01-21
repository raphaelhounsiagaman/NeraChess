"""
Tensor Shape: [channels=19, file=8, ranks=8]

Channels:

00: White Pawns
01: White Knights
02: White Bishops
03: White Rooks
04: White Queens
05: White King

06: Black Pawns
07: Black Knights
08: Black Bishops
09: Black Rooks
10: Black Queens
11: Black King

12: Side to Move

13: Can White Castle King
14: Can White Castle Queen
15: Can Black Castle King
16: Can Black Castle Queen

17: En passant file/square

18: (half move count / 50)

"""

import argparse
import os
import sys
import time
import math
import pickle
from pathlib import Path
from typing import List, Tuple

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import Dataset, DataLoader

# ----------------------------- Dataset -----------------------------
class FenDataset(Dataset):
    def __init__(self, csv_path: str, index_path: str = None, clip_pawns: float = 20.0):
        self.csv_path = Path(csv_path)
        self.clip_pawns = clip_pawns
        if index_path is None:
            self.index_path = self.csv_path.with_suffix(self.csv_path.suffix + '.idx')
        else:
            self.index_path = Path(index_path)

        if self.index_path.exists():
            with open(self.index_path, 'rb') as f:
                self.offsets = pickle.load(f)
        else:
            print(f"Building index for {self.csv_path} ... this may take a while")
            self.offsets = []
            with open(self.csv_path, 'rb') as fh:
                while True:
                    pos = fh.tell()
                    line = fh.readline()
                    if not line:
                        break
                    self.offsets.append(pos)
            with open(self.index_path, 'wb') as f:
                pickle.dump(self.offsets, f)
            print(f"Index built. {len(self.offsets)} lines. Saved to {self.index_path}")

        self._file = None

    def __len__(self):
        return len(self.offsets)

    def _open_file_if_needed(self):
        if self._file is None or self._file.closed:
            self._file = open(self.csv_path, 'r', encoding='utf-8', errors='replace')

    def __getitem__(self, idx: int):
        if idx < 0:
            idx = len(self) + idx
        offset = self.offsets[idx]
        self._open_file_if_needed()
        self._file.seek(offset)
        line = self._file.readline()
        parts = line.rsplit(',', 1)
        if len(parts) != 2:
            raise ValueError(f"Malformed line: {line!r}")
        fen_str = parts[0].strip()
        eval_str = parts[1].strip()
        try:
            target_cp = int(eval_str)
        except Exception:
            target_cp = int(eval_str.replace('+', '').split()[0])
        target = float(target_cp) / 100.0
        # clip to [-clip_pawns, clip_pawns]
        target = max(min(target, self.clip_pawns), -self.clip_pawns)
        tensor = fen_to_tensor(fen_str)
        return tensor, torch.tensor([target], dtype=torch.float32)

# ----------------------------- FEN -> Tensor -----------------------------
_piece_map = {
    'P': 0,'N': 1,'B': 2,'R': 3,'Q': 4,'K': 5,
    'p': 6,'n': 7,'b': 8,'r': 9,'q': 10,'k': 11,
}

def fen_to_tensor(fen: str) -> torch.Tensor:
    fields = fen.split()
    if len(fields) < 6:
        raise ValueError(f"FEN does not have 6 fields: {fen}")
    board, active_color, castling, enpassant, halfmove_clock, _ = fields[:6]
    out = torch.zeros((19, 8, 8), dtype=torch.float32)
    rows = board.split('/')
    for r_idx, rank in enumerate(rows):
        file_idx = 0
        for ch in rank:
            if ch.isdigit():
                file_idx += int(ch)
            else:
                c = _piece_map.get(ch)
                if c is not None:
                    out[c, file_idx, r_idx] = 1.0 
                    #out[c, r_idx, file_idx] = 1.0 # old arrangement of data 
                    file_idx += 1
    if active_color == 'w':
        out[12, :, :] = 1.0
    if 'K' in castling: out[13, :, :] = 1.0
    if 'Q' in castling: out[14, :, :] = 1.0
    if 'k' in castling: out[15, :, :] = 1.0
    if 'q' in castling: out[16, :, :] = 1.0
    if enpassant != '-':
        file_idx = ord(enpassant[0]) - ord('a')
        if 0 <= file_idx <= 7:
            out[17, file_idx, :] = 1.0
    try:
        hm = int(halfmove_clock)
    except Exception:
        hm = 0
    out[18, :, :] = float(hm/50)
    return out

# ----------------------------- Model -----------------------------
class ResidualBlock(nn.Module):
    def __init__(self, channels: int):
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn1 = nn.BatchNorm2d(channels)
        self.conv2 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn2 = nn.BatchNorm2d(channels)
    def forward(self, x):
        out = F.relu(self.bn1(self.conv1(x)))
        out = self.bn2(self.conv2(out))
        return F.relu(x + out)

class ChessResNet(nn.Module):
    def __init__(self, in_channels=19, filters=128, blocks=8):
        super().__init__()
        self.stem_conv = nn.Conv2d(in_channels, filters, 3, padding=1, bias=False)
        self.stem_bn = nn.BatchNorm2d(filters)
        self.blocks = nn.Sequential(*[ResidualBlock(filters) for _ in range(blocks)])
        self.val_conv = nn.Conv2d(filters, 32, 1, bias=False)
        self.val_bn = nn.BatchNorm2d(32)
        self.val_fc1 = nn.Linear(32*8*8, 128)
        self.val_fc2 = nn.Linear(128, 1)
    def forward(self, x):
        x = F.relu(self.stem_bn(self.stem_conv(x)))
        x = self.blocks(x)
        v = F.relu(self.val_bn(self.val_conv(x)))
        v = v.view(v.size(0), -1)
        v = F.relu(self.val_fc1(v))
        v = self.val_fc2(v)
        return v.squeeze(1)

# ----------------------------- Training -----------------------------
def save_checkpoint(state: dict, out_dir: Path, step: int):
    out_dir.mkdir(parents=True, exist_ok=True)
    p = out_dir / f"ckpt_step_{step:08d}.pt"
    torch.save(state, p)
    print(f"Saved checkpoint {p}")

def collate_fn(batch):
    inputs = torch.stack([b[0] for b in batch])
    targets = torch.cat([b[1] for b in batch]).view(-1)
    return inputs, targets

def train(args):
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    print(f"Device: {device}")
    dataset = FenDataset(args.csv, clip_pawns=20.0)
    loader = DataLoader(dataset, batch_size=args.batch_size, shuffle=True, num_workers=args.workers,
                        pin_memory=True if device.type=='cuda' else False, collate_fn=collate_fn)
    model = ChessResNet(in_channels=19, filters=args.filters, blocks=args.blocks).to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)
    scaler = torch.amp.GradScaler('cuda', enabled=(device.type=='cuda'))
    loss_fn = nn.MSELoss()

    start_step = 0
    history = []
    if args.resume:
        ckpt = torch.load(args.resume, map_location=device)
        model.load_state_dict(ckpt['model'])
        optimizer.load_state_dict(ckpt.get('opt', optimizer.state_dict()))
        start_step = ckpt.get('step', 0)
        history = ckpt.get('history', [])
        print(f"Resumed from {args.resume} at step {start_step}")

    model.train()
    global_step = start_step
    try:
        for epoch in range(args.epochs):
            epoch_loss = 0.0
            t0 = time.time()
            for i, (inp, target) in enumerate(loader):
                global_step += 1
                inp, target = inp.to(device), target.to(device)
                optimizer.zero_grad()
                with torch.amp.autocast('cuda', enabled=(device.type=='cuda')):
                    pred = model(inp)
                    loss = loss_fn(pred, target)
                scaler.scale(loss).backward()
                scaler.unscale_(optimizer)
                torch.nn.utils.clip_grad_norm_(model.parameters(), 5.0)
                scaler.step(optimizer)
                scaler.update()

                batch_loss = loss.item()
                epoch_loss += batch_loss
                rmse = math.sqrt(batch_loss)
                rmse_cp = rmse * 100

                if global_step % args.log_every == 0:
                    avg = epoch_loss / (i+1)
                    avg_rmse = math.sqrt(avg)
                    print(f"Epoch {epoch+1}/{args.epochs} Step {global_step} Batch {i+1}/{len(loader)} "
                          f"loss={batch_loss:.6f} rmse={rmse:.6f} pawns ({rmse_cp:.0f} cp) "
                          f"avg_loss={avg:.6f} avg_rmse={avg_rmse:.10f} pawns ({avg_rmse*100:.0f} cp)")

                if global_step % args.save_every == 0:
                    state = {'model': model.state_dict(), 'opt': optimizer.state_dict(), 'step': global_step, 'history': history}
                    save_checkpoint(state, Path(args.checkpoint_dir), global_step)
                if args.max_steps and global_step >= args.max_steps:
                    break
            epoch_time = time.time() - t0
            epoch_avg = epoch_loss / len(loader)
            history.append((global_step, epoch_avg))
            print(f"Finished epoch {epoch+1} avg_loss={epoch_avg:.6f} avg_rmse={math.sqrt(epoch_avg):.3f} pawns time={epoch_time:.1f}s")
            if args.max_steps and global_step >= args.max_steps:
                break
    except KeyboardInterrupt:
        print("Training interrupted")

    state = {'model': model.state_dict(), 'opt': optimizer.state_dict(), 'step': global_step, 'history': history}
    save_checkpoint(state, Path(args.checkpoint_dir), global_step)
    if args.export and device.type=='cuda':
        model.eval()
        example = torch.zeros((1,19,8,8), dtype=torch.float32).to(device)
        traced = torch.jit.trace(model, example)
        export_path = Path(args.checkpoint_dir) / 'exported_model.pt'
        traced.save(str(export_path))
        print(f"Exported scripted model to {export_path}")

# ----------------------------- CLI -----------------------------
def parse_args():
    p = argparse.ArgumentParser(description='Train chess value network from FEN CSV')
    p.add_argument('--csv', type=str, default='chessData.csv')
    p.add_argument('--batch-size', type=int, default=32)
    p.add_argument('--epochs', type=int, default=5)
    p.add_argument('--lr', type=float, default=1e-4)
    p.add_argument('--workers', type=int, default=5)
    p.add_argument('--filters', type=int, default=128)
    p.add_argument('--blocks', type=int, default=8)
    p.add_argument('--save-every', type=int, default=2000)
    p.add_argument('--log-every', type=int, default=200)
    p.add_argument('--checkpoint-dir', type=str, default='checkpoints')
    p.add_argument('--resume', type=str, default='')
    p.add_argument('--export', action='store_true')
    p.add_argument('--max-steps', type=int, default=0)
    args = p.parse_args()
    if args.max_steps==0: args.max_steps=None
    return args

if __name__ == '__main__':
    args = parse_args()
    train(args)