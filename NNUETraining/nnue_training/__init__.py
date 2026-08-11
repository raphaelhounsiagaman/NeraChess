"""Training pipeline for the NeraChess NNUE network.

The engine-side inference library lives in ``../NeraChessNNUE``. This package
owns everything that produces a ``.nnue`` file it can load:

``architecture``  network shape, mirrored from NetworkArchitecture.h
``features``      feature indexing, mirrored from FeatureSet.cpp
``serialize``     the ``.nnue`` container, mirrored from NetworkFormat.h
``quantize``      float parameters to int16, plus a reference forward pass
``engine``        UCI client for a built NeraChessUCI binary
``dataset``       training samples and batching
``datagen``       self-play data generation
``model``         the PyTorch model (requires PyTorch)
``loss``          the training objective (requires PyTorch)
``train``         training entry point (requires PyTorch)
``verify``        checks the engine and the trainer agree on a network

Only ``model``, ``loss``, and ``train`` need PyTorch. Everything required to
read, write, and verify a network runs on the standard library alone, so the
format stays testable in CI without a heavyweight install.
"""

from __future__ import annotations

__all__ = [
    "architecture",
    "dataset",
    "datagen",
    "engine",
    "features",
    "quantize",
    "serialize",
    "verify",
]

__version__ = "0.1.0"
