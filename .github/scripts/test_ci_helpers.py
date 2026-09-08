"""Standard-library regression tests for CI cache and action-pin helpers."""

import importlib.util
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock


SCRIPTS = Path(__file__).parent
WORKFLOWS = SCRIPTS.parent / "workflows"


def load_module(name: str, filename: str):
    """Load a CI helper whose filename is not an importable package name."""
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


IDENTITY = load_module("compiler_cache_identity", "compiler-cache-identity.py")
PINS = load_module("check_action_pins", "check_action_pins.py")


class CompilerCacheIdentityTests(unittest.TestCase):
    """Verify cache identity inputs are reproducible and complete."""

    def _environment(self, directory: Path, image_version: str = "20260908.1") -> dict[str, str]:
        """Create executable compiler/build-tool fixtures and their environment."""
        for name in ("cc", "cxx", "cmake", "ninja"):
            executable = directory / name
            executable.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            executable.chmod(executable.stat().st_mode | stat.S_IXUSR)
        return {
            "CC": "cc",
            "CXX": "cxx",
            "PATH": str(directory),
            "ImageOS": "ubuntu26",
            "ImageVersion": image_version,
        }

    def test_identity_is_deterministic(self) -> None:
        """The same installed tools and runner metadata produce the same digest."""
        with tempfile.TemporaryDirectory() as temporary:
            environment = self._environment(Path(temporary))
            with mock.patch.object(IDENTITY.Path, "read_bytes", return_value=b"os-release"), mock.patch.object(
                IDENTITY.subprocess, "check_output", return_value=b"tool-output\n"
            ):
                first = IDENTITY.compiler_identity(environment)
                second = IDENTITY.compiler_identity(environment)
        self.assertEqual(first, second)

    def test_runner_image_changes_identity(self) -> None:
        """Runner image revisions partition caches even when tool output is identical."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with mock.patch.object(IDENTITY.Path, "read_bytes", return_value=b"os-release"), mock.patch.object(
                IDENTITY.subprocess, "check_output", return_value=b"tool-output\n"
            ):
                older = IDENTITY.compiler_identity(self._environment(directory, "20260908.1"))
                newer = IDENTITY.compiler_identity(self._environment(directory, "20260909.1"))
        self.assertNotEqual(older, newer)

    def test_compiler_content_and_version_change_identity(self) -> None:
        """Compiler executable bytes and reported versions independently partition caches."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            environment = self._environment(directory)

            with mock.patch.object(IDENTITY.subprocess, "check_output", return_value=b"fixed\n"):
                original = IDENTITY.compiler_identity(environment)
                compiler = directory / "cc"
                compiler.write_text("#!/bin/sh\necho changed\n", encoding="utf-8")
                changed_content = IDENTITY.compiler_identity(environment)
            self.assertNotEqual(original, changed_content)

            version = [b"gcc fixture 1\n"]

            def command_output(command: list[str]) -> bytes:
                """Vary only the CC version while keeping every other tool output fixed."""
                if command == [str(directory / "cc"), "--version"]:
                    return version[0]
                return b"fixed\n"

            with mock.patch.object(IDENTITY.subprocess, "check_output", side_effect=command_output):
                first_version = IDENTITY.compiler_identity(environment)
                version[0] = b"gcc fixture 2\n"
                second_version = IDENTITY.compiler_identity(environment)
            self.assertNotEqual(first_version, second_version)

    def test_invalid_baseline_and_missing_compiler_are_rejected(self) -> None:
        """Malformed manifests and absent CC/CXX inputs fail before cache output."""
        with tempfile.TemporaryDirectory() as temporary:
            manifest = Path(temporary) / "vcpkg.json"
            manifest.write_text('{"builtin-baseline": "not-a-commit"}', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "40-character"):
                IDENTITY.vcpkg_baseline(manifest)
        with self.assertRaisesRegex(ValueError, "CC must"):
            IDENTITY.resolve_compiler("CC", {})

    def test_main_failure_does_not_append_partial_outputs(self) -> None:
        """Invalid manifests and compiler inputs leave an existing Actions output untouched."""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            output = directory / "github-output"
            output.write_text("existing=value\n", encoding="utf-8")
            manifest = directory / "vcpkg.json"

            def assert_unchanged_after_failure(contents: str) -> None:
                """Run main in a fixture repository and require output-file atomicity on failure."""
                manifest.write_text(contents, encoding="utf-8")
                previous_directory = Path.cwd()
                try:
                    os.chdir(directory)
                    with mock.patch.dict(
                        IDENTITY.os.environ,
                        {"GITHUB_OUTPUT": str(output), "PATH": str(directory)},
                        clear=True,
                    ):
                        with self.assertRaises((ValueError, KeyError)):
                            IDENTITY.main()
                finally:
                    os.chdir(previous_directory)
                self.assertEqual(output.read_text(encoding="utf-8"), "existing=value\n")

            assert_unchanged_after_failure('{"builtin-baseline": "invalid"}')
            assert_unchanged_after_failure(
                '{"builtin-baseline": "0123456789abcdef0123456789abcdef01234567"}'
            )


class ActionPinTests(unittest.TestCase):
    """Verify local actions are exempt and remote actions require immutable pins."""

    def _write(self, root: Path, relative: str, contents: str) -> None:
        """Create a minimal workflow/action fixture below *root*."""
        source = root / relative
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(contents, encoding="utf-8")

    def test_detects_floating_remote_action_only(self) -> None:
        """Floating remote actions are reported while local and Docker uses are accepted."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write(
                root,
                ".github/workflows/test.yml",
                "steps:\n  - uses: actions/checkout@v7\n  - uses: ./local\n  - uses: docker://alpine:3\n",
            )
            self._write(root, ".github/actions/setup/action.yml", "runs:\n  using: composite\n")
            invalid = PINS.floating_action_references(root)
        self.assertEqual(len(invalid), 1)
        self.assertIn("actions/checkout@v7", invalid[0])

    def test_accepts_full_commit_pin(self) -> None:
        """A 40-character lowercase commit SHA satisfies the pinning rule."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self._write(
                root,
                ".github/workflows/test.yml",
                "steps:\n  - uses: actions/checkout@0123456789abcdef0123456789abcdef01234567\n",
            )
            self._write(root, ".github/actions/setup/action.yml", "runs:\n  using: composite\n")
            self.assertEqual(PINS.floating_action_references(root), [])

    def test_repository_actions_are_pinned(self) -> None:
        """The checked-in workflows and composite actions contain no floating pins."""
        self.assertEqual(PINS.floating_action_references(SCRIPTS.parent.parent), [])


class WorkflowContractTests(unittest.TestCase):
    """Protect the intended full-check and integration-smoke partition."""

    def test_full_workflows_keep_non_push_acceptance_events(self) -> None:
        """PR, monthly, and manual full checks do not also trigger on branch pushes."""
        for filename in ("linux-ci.yml", "python-and-package-consumer.yml"):
            workflow = (WORKFLOWS / filename).read_text(encoding="utf-8")
            self.assertIn("pull_request:", workflow)
            self.assertIn("schedule:", workflow)
            self.assertIn("workflow_dispatch:", workflow)
            self.assertNotIn("\n  push:", workflow)

    def test_smoke_is_gcc_debug_push_only_and_shares_ccache_namespace(self) -> None:
        """Branch smoke is limited to GCC Debug and reuses the full Debug cache prefix."""
        smoke = (WORKFLOWS / "integration-smoke.yml").read_text(encoding="utf-8")
        self.assertIn("branches: [dev, main]", smoke)
        self.assertNotIn("pull_request:", smoke)
        self.assertNotIn("schedule:", smoke)
        self.assertNotIn("workflow_dispatch:", smoke)
        self.assertIn("CC: /usr/bin/gcc", smoke)
        self.assertIn("CXX: /usr/bin/g++", smoke)
        self.assertNotIn("clang", smoke.lower())
        self.assertIn("cmake --build --preset ci-integration-smoke", smoke)
        self.assertIn("--no-tests=error", smoke)

        debug_namespace = "ccache-v7-${{ runner.os }}-${{ runner.arch }}-ci-linux-gcc-debug-"
        python_consumer = (WORKFLOWS / "python-and-package-consumer.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn(debug_namespace, smoke)
        self.assertIn(debug_namespace, python_consumer)

    def test_dependency_archives_have_exact_configure_time_writer_keys(self) -> None:
        """Only the Debug full job and smoke save immutable dependency archives."""
        linux = (WORKFLOWS / "linux-ci.yml").read_text(encoding="utf-8")
        smoke = (WORKFLOWS / "integration-smoke.yml").read_text(encoding="utf-8")
        action = (SCRIPTS.parent / "actions/setup-linux/action.yml").read_text(encoding="utf-8")
        self.assertIn("name: Debug\n            preset: ci-linux-gcc-debug\n            writes_shared_caches: true", linux)
        self.assertIn("name: Release\n            preset: ci-linux-gcc-release\n            writes_shared_caches: false", linux)
        self.assertGreater(linux.index("- name: Save vcpkg binary archive cache"), linux.index("- name: Configure"))
        self.assertLess(linux.index("- name: Save vcpkg binary archive cache"), linux.index("- name: Build"))
        self.assertGreater(smoke.index("- name: Save vcpkg binary archive cache"), smoke.index("- name: Configure"))
        self.assertLess(smoke.index("- name: Save vcpkg binary archive cache"), smoke.index("- name: Build"))
        self.assertIn("vcpkg-binaries-v3-", action)
        self.assertNotIn("github.run_id", action)


if __name__ == "__main__":
    unittest.main()
