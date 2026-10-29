"""Check supported MMA fragment boundaries after typed template composition."""
from unittest import TestCase
from unittest.mock import patch

from ptx_frontend.code_gen.emit.tcgen_mma_collector import render_collector_header, render_collector_source
from ptx_frontend.code_gen.emit.tcgen_mma_operations import render_tcgen_mma_header, render_tcgen_mma_source
from ptx_frontend.code_gen.emit.tcgen_mma_sparse import render_sparse_header, render_sparse_source
from ptx_frontend.code_gen.emit.tcgen_mma_sparse_mx import render_sparse_mx_header, render_sparse_mx_source
from ptx_frontend.code_gen.emit.tcgen_mma_ws import render_ws_header, render_ws_source


class TcgenRenderModelTests(TestCase):
    """Preserve declaration independence and supported child-fragment output."""

    def test_declarations_do_not_validate_source_catalogue(self) -> None:
        """Header rendering remains available independently of source validation."""

        with patch('ptx_frontend.spec.tcgen_mma_operations.validate_catalogue', side_effect=ValueError('invalid catalogue')):
            for render in (render_tcgen_mma_header, render_sparse_header,
                           render_sparse_mx_header, render_collector_header, render_ws_header):
                with self.subTest(renderer=render.__name__):
                    self.assertIn('namespace ptx_frontend::resolved_ir', render())
            for render in (render_tcgen_mma_source, render_sparse_source, render_sparse_mx_source, render_ws_source):
                with self.subTest(renderer=render.__name__), self.assertRaisesRegex(ValueError, 'invalid catalogue'):
                    render()

    def test_standalone_fragments_match_artifact_composition(self) -> None:
        """Static imports preserve each supported child's exact fragment bytes."""

        for artifact, fragments in (
            (render_tcgen_mma_header, (render_sparse_header, render_sparse_mx_header, render_collector_header, render_ws_header)),
            (render_tcgen_mma_source, (render_sparse_source, render_sparse_mx_source, render_collector_source, render_ws_source)),
        ):
            output = artifact()
            self.assertTrue(output.endswith(''.join(render() for render in fragments)))
