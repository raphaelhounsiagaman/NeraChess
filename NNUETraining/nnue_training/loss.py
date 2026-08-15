"""Training objective.

NNUE is trained to predict a blend of two targets:

* the *evaluation* the search assigned to a position, which is dense and
  low-variance but only as good as the engine that produced it;
* the *game result*, which is unbiased but extremely noisy per position.

Both are mapped through a sigmoid into win-probability space before they are
compared, because the difference between +300 and +400 centipawns matters far
less than the difference between 0 and +100, and a squared error on raw
centipawns would say the opposite.

``lambda_`` picks the blend: 1.0 trains purely on evaluations, 0.0 purely on
results. Starting near 1.0 and annealing downward is the usual recipe -- early
training needs the dense signal, later training needs the unbiased one.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import architecture as arch

try:  # pragma: no cover - exercised only when PyTorch is installed
    import torch
except ImportError as error:  # pragma: no cover
    raise ImportError(
        "nnue_training.loss requires PyTorch. Install the training extras with "
        "`pip install -r requirements.txt`."
    ) from error


@dataclass
class LossConfig:
    #: Weight of the evaluation target against the game result.
    lambda_: float = 1.0

    #: Centipawn value that maps to a sigmoid input of 1.0. Larger values make
    #: the loss care about a wider evaluation range.
    scaling: float = 400.0

    #: Exponent of the error term. 2.0 is plain MSE; values slightly above it
    #: put more weight on badly wrong positions.
    power: float = 2.6

    def __post_init__(self) -> None:
        if not 0.0 <= self.lambda_ <= 1.0:
            raise ValueError("lambda_ must be within [0, 1]")
        if self.scaling <= 0.0:
            raise ValueError("scaling must be positive")
        if self.power < 1.0:
            raise ValueError("power must be at least 1")


def to_win_probability(centipawns: "torch.Tensor", scaling: float) -> "torch.Tensor":
    return torch.sigmoid(centipawns / scaling)


def nnue_loss(
    predictions: "torch.Tensor",
    scores: "torch.Tensor",
    results: "torch.Tensor",
    config: LossConfig,
) -> "torch.Tensor":
    """Blended evaluation and result loss in win-probability space.

    ``predictions`` are the model's raw outputs, in the same units the engine
    divides by ``EVAL_SCALE``; they are converted to centipawns first so that
    ``scaling`` means the same thing here as it does when reading engine output.
    """
    predicted_centipawns = predictions * arch.EVAL_SCALE

    predicted = to_win_probability(predicted_centipawns, config.scaling)
    from_score = to_win_probability(scores, config.scaling)
    target = config.lambda_ * from_score + (1.0 - config.lambda_) * results

    return torch.pow(torch.abs(predicted - target), config.power).mean()


def anneal_lambda(config: LossConfig, epoch: int, total_epochs: int, final: float) -> float:
    """Linearly moves ``lambda_`` from its starting value toward ``final``.

    Returns the value for this epoch without mutating ``config``, so a run's
    schedule stays reproducible from its configuration alone.
    """
    if total_epochs <= 1:
        return config.lambda_
    progress = min(1.0, max(0.0, epoch / (total_epochs - 1)))
    return config.lambda_ + (final - config.lambda_) * progress
