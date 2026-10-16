"""Sensitivity checks for differential comparison and explicit public schemas."""

import copy
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import run


class DifferentialChecks(unittest.TestCase):
    """Prove nested field mismatches and schema omissions are observable."""

    def test_comparator_exit_status_for_nested_fields(self):
        """Actual CLI returns nonzero for range, recovery, token and trivia deltas."""
        baseline = {"cst": {"value": {"tokens": [{"text": "ret", "leading_trivia": [
            {"text": " ", "range": {"start": {"line": 1, "column": 1}}}]}],
            "root": {"value": {"body": [{"kind": 0, "expected_kind": 9,
                                            "range": {"end": {"column": 5}}}]}}}}}
        paths = [("cst", "value", "tokens", 0, "text"),
                 ("cst", "value", "tokens", 0, "leading_trivia", 0, "text"),
                 ("cst", "value", "root", "value", "body", 0, "expected_kind"),
                 ("cst", "value", "root", "value", "body", 0, "range", "end", "column")]
        with tempfile.TemporaryDirectory() as temporary:
            reference, candidate = Path(temporary) / "reference.json", Path(temporary) / "candidate.json"
            reference.write_text(json.dumps(baseline))
            candidate.write_text(json.dumps(baseline))
            invocation = [sys.executable, str(Path(run.__file__)), "--compare-files",
                          str(reference), str(candidate)]
            self.assertEqual(subprocess.run(invocation, capture_output=True).returncode, 0)
            for path in paths:
                changed = copy.deepcopy(baseline)
                value = changed
                for key in path[:-1]:
                    value = value[key]
                value[path[-1]] = "changed" if isinstance(value[path[-1]], str) else 99
                candidate.write_text(json.dumps(changed))
                result = subprocess.run(invocation, capture_output=True, text=True)
                self.assertEqual(result.returncode, 1)
                self.assertIn(str(path[-1]), result.stdout)

    def test_schema_checks_current_public_headers(self):
        """Every current public field has a matching explicit serializer entry."""
        tool = Path(run.__file__).resolve().parent
        run.check_schema(tool.parents[1], tool)

    def test_schema_rejects_omitted_field(self):
        """A copied schema missing a concrete token range fails validation."""
        tool = Path(run.__file__).resolve().parent
        with tempfile.TemporaryDirectory() as temporary:
            copied = Path(temporary)
            schema = (tool / "snapshot_fields.inc").read_text()
            schema = schema.replace("SNAPSHOT_FIELD(token_range)\n", "", 1)
            (copied / "snapshot_fields.inc").write_text(schema)
            (copied / "snapshot.cpp").write_text((tool / "snapshot.cpp").read_text())
            with self.assertRaisesRegex(RuntimeError, "schema drift"):
                run.check_schema(tool.parents[1], copied)

    def test_field_inventory_handles_current_declaration_forms(self):
        """Constructor, enum and default syntax do not hide neighboring fields."""
        source = """struct Sample {
            int first{}; /** annotation */ std::optional<int> second;
            enum class Mode { A, B }; Mode mode = Mode::A;
            Sample(int); bool method() const;
        };"""
        self.assertEqual(run.public_fields(source, None, {"Sample": {"Sample", "method"}}),
                         {"Sample": ["first", "second", "mode"]})

    def test_schema_rejects_parenthesized_data_members(self):
        """Default factory calls and callable data members cannot disappear silently."""
        for member in ("int field = factory();", "std::function<void()> callback;",
                       "int field = sourceRange();"):
            with self.assertRaisesRegex(RuntimeError, "unsupported parenthesized"):
                run.public_fields("struct CstFile { " + member + " };", None)

    def test_schema_rejects_result_field_drift(self):
        """A new result member is found and differs from the manual serializer table."""
        tool = Path(run.__file__).resolve().parent
        source = (tool.parents[1] / "submod/common/include/source_loc.hpp").read_text()
        source = source.replace("DiagnosticCollection<D> diagnostics;",
                                "DiagnosticCollection<D> diagnostics; int added_result_field{};")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for header in run.SCHEMA_HEADERS:
                destination = root / header
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(tool.parents[1] / header, destination)
            (root / "submod/common/include/source_loc.hpp").write_text(source)
            with self.assertRaisesRegex(RuntimeError, "ResultWithDiagnostics"):
                run.check_schema(root, tool)

    def test_snapshot_rejects_duplicate_fields(self):
        """A discriminator collision cannot be accepted by JSON's last-key behavior."""
        with self.assertRaisesRegex(ValueError, "duplicate snapshot field"):
            run.decode_snapshot('{"cst":{"type":"Record","type":42}}')

    @unittest.skipUnless(sys.platform != "win32", "process probes use POSIX executables")
    def test_snapshot_detects_process_failure_and_timeout(self):
        """Fresh subprocess probes raise errors for an exit failure and a bounded hang."""
        case = {"mode": "module", "source": ""}
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "probe"
            binary.write_text("#!/bin/sh\nexit 7\n")
            binary.chmod(0o700)
            with self.assertRaises(subprocess.CalledProcessError):
                run.snapshot(binary, case)
            binary.write_text("#!/bin/sh\nexec sleep 2\n")
            with self.assertRaises(subprocess.TimeoutExpired):
                run.snapshot(binary, case, timeout=0.01)

    def test_fuzz_is_reproducible_and_bounded(self):
        """Equal seeds produce equal source bytes and every fuzz input is finite."""
        tool = Path(run.__file__).resolve().parent
        cases = run.corpus(tool, tool.parents[1], 42, 20)
        self.assertEqual(cases, run.corpus(tool, tool.parents[1], 42, 20))
        fuzz = [case for case in cases if case["name"].startswith("fuzz/")]
        self.assertEqual(len(fuzz), 20)
        self.assertTrue(all(len(bytes.fromhex(case["source_hex"])) <= 4096 for case in fuzz))

    def test_case_selection_records_exclusions(self):
        """Root and name filters combine explicitly without dropping unreported cases."""
        cases = [{"name": "valid/instruction", "mode": "instruction"},
                 {"name": "valid/module", "mode": "module"},
                 {"name": "malformed/instruction", "mode": "instruction"}]
        selected, evidence = run.select_cases(cases, "instruction", "^valid/")
        self.assertEqual(selected, cases[:1])
        self.assertEqual(evidence, {"mode": "instruction", "case_filter": "^valid/",
                                   "available_count": 3, "selected_count": 1, "excluded_count": 2})
        self.assertEqual(run.select_cases(cases, "all", None)[0], cases)
        self.assertEqual(run.select_cases(cases, "module", "^malformed/")[0], [])

    def test_frozen_corpus_has_unique_names_and_negative_families(self):
        """Explicit negative arrays stay in the default corpus with frozen provenance."""
        tool = Path(run.__file__).resolve().parent
        fixtures = json.loads((tool / "corpus.json").read_text())
        names = [case["name"] for case in fixtures]
        self.assertEqual(len(names), len(set(names)))
        operands = [case for case in fixtures if case["name"].startswith("generated-malformed-operand/")]
        negatives = [case for case in fixtures if case["name"].startswith("frozen-negative/")]
        self.assertEqual(len(operands), 24)
        self.assertEqual(len(negatives), 27)
        self.assertIn("ce29d74", operands[0]["provenance"])
        self.assertIn(run.REFERENCE, negatives[0]["provenance"])
        self.assertTrue(all(case in run.corpus(tool, tool.parents[1], 42, 0)
                            for case in operands + negatives))


if __name__ == "__main__":
    unittest.main()
