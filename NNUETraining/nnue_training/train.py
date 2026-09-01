"""Training entry point.

Usage::

    python -m nnue_training.train --data data/positions.txt --output nera.nnue

The loop is deliberately plain: one optimizer, one dataset pass per epoch, a
weight clip after every step, and an export at the end. Making it fast comes
after making a network that plays chess at all.
"""

from __future__ import annotations

import argparse
import copy
import time
from dataclasses import dataclass
from pathlib import Path

from . import architecture as arch
from . import dataset as data
from . import quantize as quant
from . import serialize


@dataclass
class TrainConfig:
    data_path: Path
    output_path: Path
    epochs: int = 20
    batch_size: int = 8192
    learning_rate: float = 1e-3
    weight_decay: float = 0.0

    #: Blend of evaluation against game result; see nnue_training.loss.
    lambda_start: float = 1.0
    lambda_end: float = 0.7

    #: Fraction held out to report a loss that is comparable across epochs.
    #: The training loss is not: when lambda anneals, the objective itself
    #: changes every epoch, so that number can rise while the network improves.
    #: The validation loss is always measured at lambda_end.
    validation_fraction: float = 0.02
    max_validation_positions: int = 50_000

    #: Weights to start from instead of a random initialization. With an
    #: identical architecture this is exact -- the run begins at the source
    #: network's strength and can only refine it -- so it is the honest way to
    #: ask whether new data improves a champion rather than replaces it.
    init_from: Path | None = None

    #: Read the pack off disk instead of into memory. None decides by size:
    #: a pack that would not comfortably fit is mapped. A pack large enough to
    #: need this runs to tens of gigabytes on a machine with seven.
    memmap: bool | None = None

    device: str = "cpu"
    seed: int = 0

    #: Write an intermediate network after every epoch, so a run that is
    #: stopped early still leaves something playable behind.
    checkpoint_every_epoch: bool = True


def export(model: object, path: Path) -> Path:
    """Quantizes a trained model and writes an engine-loadable network."""
    parameters = model.export_parameters()  # type: ignore[attr-defined]
    quantized, report = quant.quantize(
        feature_weights=parameters["feature_weights"],
        feature_bias=parameters["feature_bias"],
        output_weights=parameters["output_weights"],
        output_bias=parameters["output_bias"],
    )
    written = serialize.write(path, quantized)
    print(f"exported {written} ({arch.describe()}); {report.describe()}")
    return written


def train(config: TrainConfig) -> Path:
    # Imported here so that --help and the exporter work without PyTorch.
    import torch

    from .loss import LossConfig, anneal_lambda, nnue_loss
    from .model import NnueModel

    torch.manual_seed(config.seed)
    device = torch.device(config.device)

    print(f"loading samples from {config.data_path}")
    started = time.monotonic()
    if config.data_path.suffix == ".pack":
        cache = data.open_pack(config.data_path, memmap=config.memmap)
    else:
        cache = data.FeatureCache.from_text(config.data_path)
    if len(cache) == 0:
        raise SystemExit(f"{config.data_path} contains no samples")
    print(f"loaded {len(cache)} positions in {time.monotonic() - started:.1f}s")

    validation_size = min(
        int(len(cache) * config.validation_fraction), config.max_validation_positions
    )
    training_indices, validation_indices = cache.split(validation_size, config.seed)
    if validation_indices:
        print(
            f"holding out {len(validation_indices)} positions to measure progress "
            f"at a fixed lambda of {config.lambda_end}"
        )

    model = NnueModel().to(device)
    if config.init_from is not None:
        model.load_state_dict(torch.load(config.init_from, map_location=device))
        print(f"initialised from {config.init_from}", flush=True)
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=config.learning_rate, weight_decay=config.weight_decay
    )
    loss_config = LossConfig(lambda_=config.lambda_start)

    def to_tensors(batch: data.Batch):
        return (
            torch.tensor(batch.own_indices, dtype=torch.long, device=device),
            torch.tensor(batch.own_offsets, dtype=torch.long, device=device),
            torch.tensor(batch.their_indices, dtype=torch.long, device=device),
            torch.tensor(batch.their_offsets, dtype=torch.long, device=device),
            torch.tensor(batch.scores, dtype=torch.float32, device=device),
            torch.tensor(batch.results, dtype=torch.float32, device=device),
            torch.tensor(batch.output_buckets, dtype=torch.long, device=device),
        )

    # Always measured at lambda_end, so it means the same thing every epoch and
    # can actually be compared. The training loss cannot be, because annealing
    # changes the objective underneath it.
    fixed_config = LossConfig(
        lambda_=config.lambda_end, scaling=loss_config.scaling, power=loss_config.power
    )

    @torch.no_grad()
    def validate() -> float | None:
        if not validation_indices:
            return None
        model.eval()
        total, count = 0.0, 0
        for batch in cache.batches(config.batch_size, indices=validation_indices):
            own, own_off, their, their_off, scores, results, buckets = to_tensors(batch)
            predictions = model(own, own_off, their, their_off, buckets)
            total += float(nnue_loss(predictions, scores, results, fixed_config).item())
            count += 1
        model.train()
        return total / max(1, count)

    best_validation_loss = float("inf")
    # Weights of the best epoch by validation loss. Without this the exported
    # network is whatever the last epoch happened to produce, so a run that
    # trains past its best silently ships the overfitted end of the curve --
    # and nothing downstream would notice, because the gate blames the network
    # rather than the schedule. While validation is still improving every epoch
    # this is a no-op: the best epoch is the last one.
    best_state: dict | None = None
    best_epoch = 0

    for epoch in range(config.epochs):
        epoch_lambda = anneal_lambda(
            loss_config, epoch, config.epochs, config.lambda_end
        )
        epoch_config = LossConfig(
            lambda_=epoch_lambda,
            scaling=loss_config.scaling,
            power=loss_config.power,
        )

        started = time.monotonic()
        total_loss = 0.0
        batch_count = 0

        # Samples arrive in game order, so an unshuffled batch is a few
        # positions from the same handful of games and its gradient is badly
        # correlated. Reshuffle every epoch.
        for batch in cache.batches(
            config.batch_size,
            shuffle_seed=config.seed + epoch,
            indices=training_indices,
        ):
            own, own_off, their, their_off, scores, results, buckets = to_tensors(batch)

            predictions = model(own, own_off, their, their_off, buckets)
            loss = nnue_loss(predictions, scores, results, epoch_config)

            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            optimizer.step()
            model.clip_weights()

            total_loss += float(loss.item())
            batch_count += 1

        validation_loss = validate()
        elapsed = time.monotonic() - started
        average = total_loss / max(1, batch_count)

        # "train" moves with the annealing lambda and is not comparable between
        # epochs; "val" is the one to watch.
        line = (
            f"epoch {epoch + 1}/{config.epochs} "
            f"train {average:.6f} (lambda {epoch_lambda:.3f})"
        )
        if validation_loss is not None:
            marker = ""
            if validation_loss < best_validation_loss:
                best_validation_loss = validation_loss
                marker = " *best"
                best_state = copy.deepcopy(model.state_dict())
                best_epoch = epoch + 1
            line += f"  val {validation_loss:.6f}{marker}"
        line += f"  ({elapsed:.1f}s, {batch_count} batches)"
        print(line, flush=True)

        if config.checkpoint_every_epoch:
            checkpoint = config.output_path.with_suffix(f".epoch{epoch + 1}.nnue")
            export(model, checkpoint)

    if best_state is not None and best_epoch != config.epochs:
        print(
            f"exporting epoch {best_epoch} (val {best_validation_loss:.6f}) rather "
            f"than epoch {config.epochs}: validation stopped improving",
            flush=True,
        )
        model.load_state_dict(best_state)

    return export(model, config.output_path)


def parse_arguments(argv: list[str] | None = None) -> TrainConfig:
    parser = argparse.ArgumentParser(
        prog="nnue_training.train",
        description=f"Trains a NeraChess NNUE network ({arch.describe()}).",
    )
    parser.add_argument("--data", type=Path, required=True, help="training sample file")
    parser.add_argument(
        "--output", type=Path, default=Path("nera.nnue"), help="network to write"
    )
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch-size", type=int, default=8192)
    parser.add_argument("--learning-rate", type=float, default=1e-3)
    parser.add_argument("--weight-decay", type=float, default=0.0)
    parser.add_argument("--lambda-start", type=float, default=1.0)
    parser.add_argument("--lambda-end", type=float, default=0.7)
    parser.add_argument("--validation-fraction", type=float, default=0.02,
                        help="fraction held out to report a loss comparable "
                             "across epochs; 0 disables it")
    parser.add_argument("--device", default="cpu", help="cpu, cuda, or mps")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument(
        "--no-checkpoints", action="store_true", help="only write the final network"
    )
    parser.add_argument(
        "--init-from", type=Path, default=None,
        help="start from these weights instead of a random initialization"
    )
    parser.add_argument(
        "--memmap", dest="memmap", action="store_true", default=None,
        help="read the pack from disk rather than loading it into memory"
    )
    parser.add_argument(
        "--no-memmap", dest="memmap", action="store_false",
        help="force loading the whole pack into memory"
    )

    arguments = parser.parse_args(argv)
    return TrainConfig(
        data_path=arguments.data,
        output_path=arguments.output,
        epochs=arguments.epochs,
        batch_size=arguments.batch_size,
        learning_rate=arguments.learning_rate,
        weight_decay=arguments.weight_decay,
        lambda_start=arguments.lambda_start,
        lambda_end=arguments.lambda_end,
        validation_fraction=arguments.validation_fraction,
        device=arguments.device,
        seed=arguments.seed,
        checkpoint_every_epoch=not arguments.no_checkpoints,
        init_from=arguments.init_from,
        memmap=arguments.memmap,
    )


def main(argv: list[str] | None = None) -> int:
    train(parse_arguments(argv))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
