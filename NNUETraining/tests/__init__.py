"""Tests for the NNUE training pipeline.

Run from the NNUETraining directory::

    python3 -m unittest discover -s tests -t .

None of these tests need PyTorch; they cover the format, the feature indexing,
and the batching, which is everything the engine depends on.
"""
