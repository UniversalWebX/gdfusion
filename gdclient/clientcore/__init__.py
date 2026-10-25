"""GD Client configuration and feature metadata core."""
from .registry import FEATURE_REGISTRY, FeatureSpec, ValidationResult, validate_configuration
__all__ = ["FEATURE_REGISTRY", "FeatureSpec", "ValidationResult", "validate_configuration"]
