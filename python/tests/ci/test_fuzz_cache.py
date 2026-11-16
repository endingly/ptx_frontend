"""Check sanitizer-cache trust, validation gates, and retention isolation."""

import importlib.util
from pathlib import Path
import re
import sys
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[3]


def helper(name):
    """Load a repository maintenance helper without executing its CLI."""
    spec = importlib.util.spec_from_file_location(name, ROOT / '.github/scripts' / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def condition(expression, values, success=True):
    """Evaluate the small boolean-expression subset used by the workflow gates."""
    expression = ' '.join(expression.split()).replace('success()', str(success))
    expression = re.sub(r'\b(?:github|steps|needs)\.[A-Za-z0-9_.-]+',
                        lambda match: repr(values.get(match.group(), '')), expression)
    return eval(expression.replace('&&', ' and ').replace('||', ' or '),
                {'__builtins__': {}}, {})


class FuzzCacheTests(unittest.TestCase):
    """Protect cache isolation and all validation prerequisites for publication."""

    @classmethod
    def setUpClass(cls):
        """Read the actual workflow, retaining YAML's literal ``on`` key."""
        cls.workflow = yaml.load((ROOT / '.github/workflows/frontend-fuzz.yml').read_text(),
                                 Loader=yaml.BaseLoader)
        cls.steps = cls.workflow['jobs']['fuzz']['steps']
        cls.named = {step.get('name'): step for step in cls.steps}
        cls.save = cls.named['Save validated sanitizer compiler cache']

    def values(self, event='pull_request', ref='refs/pull/12/merge'):
        """Return a successful prefix-hit run's expression context."""
        return {'github.event_name': event, 'github.ref': ref,
                'steps.configure.outcome': 'success', 'steps.build.outcome': 'success',
                'steps.smoke.outcome': 'success', 'steps.ccache.outputs.cache-hit': 'false',
                'needs.fuzz.result': 'success', 'needs.fuzz.outputs.cache-prefix': 'profile-'}

    def test_save_event_and_hit_matrix(self):
        """Only validated PR snapshots and main-push seeds may be newly saved."""
        for event, ref, allowed in [
            ('pull_request', 'refs/pull/12/merge', True),  # Same rule for forks.
            ('push', 'refs/heads/main', True),
            ('push', 'refs/heads/topic', False),
            ('workflow_dispatch', 'refs/heads/main', False),
            ('workflow_dispatch', 'refs/heads/topic', False),
        ]:
            for hit in ['true', 'false', '']:
                with self.subTest(event=event, ref=ref, hit=hit):
                    values = self.values(event, ref)
                    values['steps.ccache.outputs.cache-hit'] = hit
                    self.assertEqual(condition(self.save['if'], values), allowed and hit != 'true')

    def test_failures_never_publish(self):
        """Configure, compile, smoke failure/skips and prior failures block saves."""
        for phase in ['configure', 'build', 'smoke']:
            for outcome in ['failure', 'cancelled', 'skipped', '']:
                values = self.values()
                values[f'steps.{phase}.outcome'] = outcome
                self.assertFalse(condition(self.save['if'], values))
        self.assertFalse(condition(self.save['if'], self.values(), success=False))
        self.assertNotIn('continue-on-error', self.named['Build fuzz targets'])
        self.assertIn('exit "$build_exit_code"', self.named['Build fuzz targets']['run'])

    def test_profile_and_validation_contract(self):
        """Keep sanitizer caches separate while preserving the complete fuzz gate."""
        restore = self.named['Restore isolated sanitizer compiler cache']
        profile = self.named['Identify isolated sanitizer cache profile']
        self.assertEqual(restore['with']['path'], '.cache/ccache-fuzz')
        self.assertEqual(restore['with'], self.save['with'] | {'restore-keys': '${{ steps.profile.outputs.prefix }}'})
        self.assertIn('frontend-fuzz-asan-${COMPILER}-${PROFILE}-', profile['run'])
        self.assertIn('compiler-identity', profile['env']['COMPILER'])
        for path in ['frontend-fuzz.yml', 'frontend_fuzz.cmake', 'CMakeLists.txt', 'vcpkg.json']:
            self.assertIn(path, profile['env']['PROFILE'])
        configure = self.named['Configure isolated sanitizer build']['run']
        for flag in ['-DPTX_USE_CCACHE=ON', '-DBUILD_TESTING=OFF',
                     '-DPTX_FRONTEND_BUILD_FUZZERS=ON', '-O1 -gline-tables-only -DNDEBUG']:
            self.assertIn(flag, configure)
        self.assertIn('--parallel 4 --target fuzz_lexer_cst fuzz_frontend fuzz_frontend_canary',
                      self.named['Build fuzz targets']['run'])
        self.assertEqual(self.named['Replay seeds and bounded fixed-seed campaigns']['run'],
                         '.venv/bin/python fuzz/run_smoke.py --runs 2000 --seconds 30')
        self.assertEqual(self.named['Upload sanitizer compiler-cache measurements']['if'], 'always()')
        paths = self.workflow['on']['pull_request']['paths']
        for path in ['.github/actions/setup-linux/action.yml', '.github/scripts/**']:
            self.assertIn(path, paths)

    def test_cleanup_trust_and_main_serialization(self):
        """The write-permission cleanup job can run only after trusted main success."""
        cleanup = self.workflow['jobs']['cleanup']
        self.assertEqual(self.workflow['permissions'], {'contents': 'read'})
        self.assertNotIn('permissions', self.workflow['jobs']['fuzz'])
        self.assertEqual(self.workflow['jobs']['fuzz']['cache-mode'], 'write')
        self.assertEqual(cleanup['permissions'], {'contents': 'read', 'actions': 'write'})
        self.assertEqual(cleanup['steps'][0]['with']['persist-credentials'], 'false')
        self.assertEqual(self.workflow['on']['push']['branches'], ['main'])
        self.assertEqual(self.workflow['concurrency']['cancel-in-progress'],
                         "${{ github.ref != 'refs/heads/main' }}")
        self.assertTrue(condition(cleanup['if'], self.values('push', 'refs/heads/main')))
        for event, ref in [('pull_request', 'refs/pull/12/merge'),
                           ('workflow_dispatch', 'refs/heads/main'), ('push', 'refs/heads/topic')]:
            self.assertFalse(condition(cleanup['if'], self.values(event, ref)))
        for field in ['needs.fuzz.result', 'needs.fuzz.outputs.cache-prefix']:
            values = self.values('push', 'refs/heads/main'); values[field] = ''
            self.assertFalse(condition(cleanup['if'], values))
        maintenance = yaml.load((ROOT / '.github/workflows/pr-cache-maintenance.yml').read_text(),
                                Loader=yaml.BaseLoader)
        self.assertIn('Frontend fuzz smoke', maintenance['on']['workflow_run']['workflows'])

    def test_retention_keeps_profiles_refs_and_versions_separate(self):
        """Existing helpers retain ordinary and sanitizer generations independently."""
        pr = helper('cleanup_pr_ccache')
        ref = 'refs/pull/12/merge'
        entries = []
        for family, start in [('ci-linux-clang-debug-tools', 1), ('frontend-fuzz-asan-tools-profile', 5)]:
            for offset in range(4):
                entries.append(pr.cache_entry({'id': start + offset,
                    'key': f'ccache-v1-Linux-X64-{family}-{offset:040x}', 'version': 'v',
                    'ref': ref, 'created_at': f'2026-11-{offset + 1:02}T00:00:00Z'}))
        self.assertEqual(pr.deletion_plan(entries, ref, 2), [1, 2, 5, 6])
        main = helper('cleanup_ccache_seeds')
        prefix = 'ccache-v1-Linux-X64-frontend-fuzz-asan-tools-profile-'
        current = prefix + f'{3:040x}'
        seeds = [main.CacheEntry(i, prefix + f'{i:040x}', 'v', 'refs/heads/main',
                                 f'2026-11-{i:02}T00:00:00Z') for i in range(1, 4)]
        seeds += [main.CacheEntry(4, prefix + f'{0:040x}', 'other-version', 'refs/heads/main', '2026-11-01T00:00:00Z'),
                  main.CacheEntry(5, prefix + f'{0:040x}', 'v', ref, '2026-11-01T00:00:00Z'),
                  main.CacheEntry(6, 'ccache-v8-Linux-X64-debug-' + f'{0:040x}', 'v', 'refs/heads/main', '2026-11-01T00:00:00Z')]
        self.assertEqual(main.deletion_plan(seeds, current, prefix, 'refs/heads/main'), [1])
        with self.assertRaises(main.CacheApiError):
            main.deletion_plan(seeds, prefix + f'{9:040x}', prefix, 'refs/heads/main')


if __name__ == '__main__':
    unittest.main()
