"""PyTorch model for the ``(768 -> 512)x2 -> 1`` network.

The model mirrors NeraChessNNUE exactly in structure so that quantizing its
parameters yields a network the engine evaluates identically:

* one feature transformer shared by both perspectives, applied as a sparse sum
  of active feature columns (the training-time equivalent of the engine's
  accumulator);
* squared clipped ReLU on the concatenated ``[own, their]`` hidden layer;
* a single affine output head.

Requires PyTorch. Everything needed to read, write, and verify a network file
lives in modules that do not.
"""

from __future__ import annotations

from typing import Any

from . import architecture as arch

try:  # pragma: no cover - exercised only when PyTorch is installed
    import torch
    from torch import nn
except ImportError as error:  # pragma: no cover
    raise ImportError(
        "nnue_training.model requires PyTorch. Install the training extras with "
        "`pip install -r requirements.txt`."
    ) from error


def squared_clipped_relu(values: "torch.Tensor") -> "torch.Tensor":
    """clamp(x, 0, 1) squared.

    The float counterpart of ``Quantization::Activate``: the engine clips at
    ``QUANTIZATION_A`` because that is where 1.0 lands after quantization.
    """
    clipped = values.clamp(0.0, 1.0)
    return clipped * clipped


def clipped_relu(values: "torch.Tensor") -> "torch.Tensor":
    return values.clamp(0.0, 1.0)


ACTIVATIONS = {
    arch.Activation.SQUARED_CLIPPED_RELU: squared_clipped_relu,
    arch.Activation.CLIPPED_RELU: clipped_relu,
}


class NnueModel(nn.Module):
    """Trainable form of the engine's network."""

    def __init__(self) -> None:
        super().__init__()

        # EmbeddingBag sums the columns of the active features, which is
        # exactly what the engine's accumulator holds. Its weight is stored
        # [feature][neuron]; the engine wants the same order, so no transpose
        # happens on the export path.
        self.feature_transformer = nn.EmbeddingBag(
            arch.TOTAL_INPUT_SIZE, arch.HIDDEN_SIZE, mode="sum"
        )
        self.feature_bias = nn.Parameter(torch.zeros(arch.HIDDEN_SIZE))
        self.output = nn.Linear(arch.OUTPUT_INPUT_SIZE, arch.OUTPUT_BUCKET_COUNT)

        self.activation = ACTIVATIONS[arch.ACTIVATION]
        self.reset_parameters()

    def reset_parameters(self) -> None:
        # Small initial feature weights keep the accumulator inside the
        # activation's clipping range from the first step.
        nn.init.normal_(self.feature_transformer.weight, std=0.01)
        nn.init.zeros_(self.feature_bias)
        nn.init.normal_(self.output.weight, std=0.05)
        nn.init.zeros_(self.output.bias)

    def accumulate(
        self, indices: "torch.Tensor", offsets: "torch.Tensor"
    ) -> "torch.Tensor":
        """Hidden layer for one perspective from flat feature indices.

        ``indices`` is every active feature of the batch concatenated;
        ``offsets[i]`` is where sample ``i`` starts. This is the same layout a
        sparse collate function produces and avoids materializing a dense
        768-wide input per sample.
        """
        return self.feature_transformer(indices, offsets) + self.feature_bias

    def forward(
        self,
        own_indices: "torch.Tensor",
        own_offsets: "torch.Tensor",
        their_indices: "torch.Tensor",
        their_offsets: "torch.Tensor",
        output_buckets: "torch.Tensor | None" = None,
    ) -> "torch.Tensor":
        """Returns one raw output per sample, before the centipawn scaling.

        The side to move always occupies the first half of the output layer's
        input, matching ``Network::Forward``.
        """
        own = self.accumulate(own_indices, own_offsets)
        their = self.accumulate(their_indices, their_offsets)
        hidden = self.activation(torch.cat([own, their], dim=1))
        outputs = self.output(hidden)

        if arch.OUTPUT_BUCKET_COUNT == 1:
            return outputs.squeeze(1)
        if output_buckets is None:
            raise ValueError("output buckets are required for a multi-head network")
        return outputs.gather(1, output_buckets.unsqueeze(1)).squeeze(1)

    @torch.no_grad()
    def clip_weights(self) -> None:
        """Keeps parameters inside the range int16 quantization can represent.

        Without this, training drifts into weights that saturate on export and
        the quantized network plays measurably worse than the float one.
        """
        feature_limit = 32767.0 / arch.QUANTIZATION_A
        output_limit = 32767.0 / arch.QUANTIZATION_B
        self.feature_transformer.weight.clamp_(-feature_limit, feature_limit)
        self.feature_bias.clamp_(-feature_limit, feature_limit)
        self.output.weight.clamp_(-output_limit, output_limit)

        bias_limit = 32767.0 / (arch.QUANTIZATION_A * arch.QUANTIZATION_B)
        self.output.bias.clamp_(-bias_limit, bias_limit)

    def export_parameters(self) -> dict[str, Any]:
        """Parameters in the shapes :func:`nnue_training.quantize.quantize` wants."""
        return {
            # quantize() expects torch Linear order, [neuron][feature].
            "feature_weights": self.feature_transformer.weight.t(),
            "feature_bias": self.feature_bias,
            "output_weights": self.output.weight,
            "output_bias": self.output.bias,
        }
