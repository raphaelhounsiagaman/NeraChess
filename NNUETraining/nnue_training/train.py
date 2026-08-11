"""Training entry point.

Usage::

    python -m nnue_training.train --data data/positions.txt --output nera.nnue

The loop is deliberately plain: one optimizer, one dataset pass per epoch, a
weight clip after every step, and an export at the end. Making it fast comes
after making a network that plays chess at all.
"""

from __future__ import annotations

import argparse
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
        cache = data.FeatureCache.load(config.data_path)
    else:
        cache = data.FeatureCache.from_text(config.data_path)
    if len(cache) == 0:
        raise SystemExit(f"{config.data_path} contains no samples")
    print(f"loaded {len(cache)} positions in {time.monotonic() - started:.1f}s")

    model = NnueModel().to(device)
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=config.learning_rate, weight_decay=config.weight_decay
    )
    loss_config = LossConfig(lambda_=config.lambda_start)

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

        # Self-play samples arrive in game order, so an unshuffled batch is a
        # few positions from the same handful of games and its gradient is
        # badly correlated. Reshuffle every epoch.
        for batch in cache.batches(config.batch_size, shuffle_seed=config.seed + epoch):
            own_indices = torch.tensor(batch.own_indices, dtype=torch.long, device=device)
            own_offsets = torch.tensor(batch.own_offsets, dtype=torch.long, device=device)
            their_indices = torch.tensor(
                batch.their_indices, dtype=torch.long, device=device
            )
            their_offsets = torch.tensor(
                batch.their_offsets, dtype=torch.long, device=device
            )
            scores = torch.tensor(batch.scores, dtype=torch.float32, device=device)
            results = torch.tensor(batch.results, dtype=torch.float32, device=device)

            predictions = model(own_indices, own_offsets, their_indices, their_offsets)
            loss = nnue_loss(predictions, scores, results, epoch_config)

            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            optimizer.step()
            model.clip_weights()

            total_loss += float(loss.item())
            batch_count += 1

        elapsed = time.monotonic() - started
        average = total_loss / max(1, batch_count)
        print(
            f"epoch {epoch + 1}/{config.epochs} "
            f"loss {average:.6f} lambda {epoch_lambda:.3f} "
            f"({elapsed:.1f}s, {batch_count} batches)"
        )

        if config.checkpoint_every_epoch:
            checkpoint = config.output_path.with_suffix(f".epoch{epoch + 1}.nnue")
            export(model, checkpoint)

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
    parser.add_argument("--device", default="cpu", help="cpu, cuda, or mps")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument(
        "--no-checkpoints", action="store_true", help="only write the final network"
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
        device=arguments.device,
        seed=arguments.seed,
        checkpoint_every_epoch=not arguments.no_checkpoints,
    )


def main(argv: list[str] | None = None) -> int:
    train(parse_arguments(argv))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
