"""Boundaries introduced by package-loaded, strict C++ template rendering."""
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory
import os
import unittest

from jinja2 import TemplateNotFound, UndefinedError

from ptx_frontend.code_gen.cpp_literals import cpp_string
from ptx_frontend.code_gen.rendering import render_template
from ptx_frontend.code_gen.render_models.resolved_dispatch import DispatchEntry, DispatchModel


@dataclass(frozen=True)
class EmptyModel:
    """A deliberately incomplete concrete root for strict-field validation."""


class RenderingTests(unittest.TestCase):
    """Verify resource, undefined-value, literal and per-render state contracts."""

    def test_missing_template_and_model_fields_fail_explicitly(self) -> None:
        with self.assertRaises(TemplateNotFound):
            render_template('absent-template.j2', EmptyModel())
        with self.assertRaises(UndefinedError):
            render_template('resolved/dispatch.cpp.j2', EmptyModel())
        with self.assertRaises(TypeError):
            render_template('resolved/dispatch.cpp.j2', {})

    def test_cpp_literal_preserves_descriptor_escaping_contract(self) -> None:
        self.assertEqual(cpp_string('a\\b"c'), '"a\\\\b\\"c"')
        self.assertEqual(cpp_string('é\n'), '"é\n"')

    def test_package_rendering_is_cwd_independent_and_keeps_lf(self) -> None:
        model = DispatchModel('custom/root', (DispatchEntry('a', 'A'),))
        previous = Path.cwd()
        try:
            with TemporaryDirectory() as directory:
                os.chdir(directory)
                text = render_template('resolved/dispatch.cpp.j2', model)
        finally:
            os.chdir(previous)
        self.assertIn('#include <custom/root/ptx_instruction_catalogue.gen.hpp>', text)
        self.assertIn('case Opcode::A: return "a";', text)
        self.assertTrue(text.endswith('\n'))
        self.assertNotIn('\r', text)

    def test_concurrent_render_contexts_do_not_share_models(self) -> None:
        models = tuple(DispatchModel('root', (DispatchEntry(f'op{index}', f'Op{index}'),)) for index in range(24))
        with ThreadPoolExecutor(max_workers=6) as executor:
            texts = tuple(executor.map(lambda model: render_template('resolved/dispatch.cpp.j2', model), models))
        for index, text in enumerate(texts):
            self.assertIn(f'case Opcode::Op{index}: return "op{index}";', text)
            self.assertEqual(text.count('case Opcode::'), 1)


if __name__ == '__main__':
    unittest.main()
