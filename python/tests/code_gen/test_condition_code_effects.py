"""Canonical implicit-state contracts for extended-precision arithmetic."""

import unittest

from ptx_frontend.code_gen.model import ConditionCodeEffect


class ConditionCodeEffectsTest(unittest.TestCase):
    """Ensure the condition-code effect enum rejects unknown source spellings."""

    def test_unknown_semantic_value_is_rejected(self):
        """The normalized effect domain cannot retain arbitrary spelling."""

        with self.assertRaises(ValueError):
            ConditionCodeEffect("implicit_carry_maybe")
