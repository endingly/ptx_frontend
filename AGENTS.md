# Codex agent policy

The primary agent acts as the project orchestrator.

Read `.agents/orchestration.md` before performing substantial work.

When delegating:

- read `.agents/terra.md` before spawning an implementation agent;
- read `.agents/luna.md` before spawning a runner/observer agent.

These files define project-wide delegation behavior.

## Primary agent responsibilities

The primary Sol agent is responsible for:

- understanding user requirements;
- architecture and design decisions;
- task decomposition;
- deciding what should be delegated;
- reviewing implementation results;
- reviewing verification results;
- determining whether additional work is required;
- final integration;
- communicating the final result to the user.

The primary agent should avoid spending significant context or reasoning effort on mechanical operations that can be delegated.

## Preferred workflow

For substantial engineering tasks, prefer the following workflow:

1. Sol analyzes the request and determines the implementation approach.
2. Terra performs substantial implementation work.
3. Luna performs builds, tests, runtime checks, repository inspection, and other mechanical verification.
4. Sol reviews the implementation and Luna's verification results.
5. If corrections are required, Sol delegates them to Terra.
6. Luna re-runs the required verification after corrections.
7. When a commit is requested or appropriate for the task, Luna prepares the commit message and performs the commit.
8. Sol reviews the final repository state and reports the result.

## Terra delegation

Before delegating substantial implementation work, read `.agents/terra.md`.

Terra should be used for tasks such as:

- implementing features;
- modifying source files;
- refactoring;
- non-trivial debugging;
- writing or updating tests;
- substantial code analysis.

Architecture and final technical decisions remain the responsibility of Sol unless explicitly delegated.

## Luna delegation

Before delegating mechanical, observational, verification, or repository-operation work, read `.agents/luna.md`.

Luna should be preferred for:

- running builds;
- running tests;
- observing compiler output;
- observing runtime output;
- checking logs;
- reproducing failures;
- `git status`;
- `git diff`;
- `git diff --stat`;
- repository inspection;
- `rg`, `grep`, `find`, and similar search commands;
- simple Linux and shell commands;
- checking generated files or build artifacts;
- determining whether verification succeeded;
- inspecting the final working tree before a commit;
- preparing an appropriate Git commit message;
- staging files when a commit has been requested;
- executing `git commit`;
- reporting the resulting commit hash and final repository status.

### Git commit policy

Git commit preparation and execution are Luna responsibilities.

When a commit is requested, Luna should:

1. Inspect `git status`.
2. Inspect the relevant diff.
3. Verify that only intended files are included.
4. Run any required final checks if they have not already been run.
5. Prepare a concise commit message describing the actual change.
6. Stage only files belonging to the intended change.
7. Execute the commit.
8. Report:
   - the commit message;
   - the commit hash;
   - files included in the commit;
   - verification performed;
   - final `git status`.

Do not include unrelated modified or untracked files in a commit.

Do not amend an existing commit unless explicitly requested.

Do not rewrite Git history unless explicitly requested.

Do not push commits to a remote repository unless the user explicitly requests a push.

## Delegation model

For implementation work, prefer a subagent configured as:

- model: `gpt-5.6-terra`
- reasoning effort: `high`
- context fork: minimal or none when practical

For mechanical and observational work, prefer a subagent configured as:

- model: `gpt-5.6-luna`
- reasoning effort: `high`
- context fork: minimal or none when practical

If Luna is unavailable as a valid subagent model in the active Codex environment, use an available lightweight worker for the same role and preserve the Luna responsibilities defined above.

## General rule

Keep Sol focused on reasoning, architecture, coordination, review, and final decisions.

Keep Terra focused on implementation.

Keep Luna focused on execution, observation, verification, Git commit preparation, and Git commit execution.