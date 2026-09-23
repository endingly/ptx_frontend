# Codex agent policy

The primary agent coordinates the requested outcome, task routing, integration,
and communication. Sol owns architecture control and core acceptance review,
regardless of the primary model. The primary may implement, verify, and close
non-core work within established contracts; it must not substitute its own
approval for a required Sol decision. Model/effort selection is defined in the
[registry](.agents/orchestration.md#model-preferences); it does not reconfigure
the host session.

## Working rules

- Before delegating, read the routing, model registry, and task-packet sections
  of [.agents/orchestration.md](.agents/orchestration.md). Read only the assigned
  role and the references needed for the action; do not recursively load every
  linked document. Apply the registry to every worker, retry, resume, and fallback.
- For a proposed architecture/core-contract change or its acceptance review, use
  [ptx-core-review](.agents/skills/ptx-core-review/SKILL.md). The Sol gate applies
  even when the host does not discover skills. Simple explanations, spelling-only
  edits, and non-core checks do not trigger this workflow. When classification is
  uncertain, consult [the core boundary](.agents/sol.md#core-boundary).
- Follow the user's current request and applicable system/developer rules.
  Repository policies and skills do not expand task authorization.
- Continue already-authorized work through implementation and verification.
  Resolve routine choices from repository evidence; ask only when missing
  information materially changes scope, correctness, or an irreversible action.
- A findings-only audit or explanation is read-only. When the same request also
  authorizes fixes, implement and verify that scope without asking again. Ask
  only for a real missing decision or authorization, not each routine repair.
- Preserve unrelated work and the user's staging choices. Stage task files
  only when a commit is authorized; include all changes only when requested.
- Commit and push are separate actions. Push requires an explicit request;
  do not amend, rewrite history, or discard changes without authorization.
- Use existing components before adding abstractions. Keep changes scoped to
  the requested behavior.

## Code documentation and templates

- Add Doxygen comments to every newly introduced C++ function, class, and struct.
- Document important variables and data members: meaning, ownership/lifetime,
  units, and invariants where relevant. Use docstrings for Python APIs.
- Explain useful contracts rather than restating identifiers.
- Source comments must not mention milestone or work-package identifiers.
- Constrain template parameters with meaningful concepts/requires clauses;
  prefer overloads when the supported type set is small and fixed.

## Documentation authority

- [.agents/project_roadmap.v2.md](.agents/project_roadmap.v2.md) records frontend
  scope, architecture boundaries, and branch-verifiable milestone status.
  Read the relevant sections before changing a planned contract.
- [README.md](README.md) and the design documents in [docs/us-en/](docs/us-en/)
  and [docs/zh-han/](docs/zh-han/) describe the public frontend and generator
  contracts. Keep corresponding language versions aligned when updating them.
- Use current code and reproducible checks to resolve documentation drift.
  Do not infer feature completion from a branch name or a hosted review result.
- Documents in [docs/deprecated/](docs/deprecated/) and historical review
  records are evidence, not instructions to resume retired work.

## Verification and reporting

- Run checks appropriate to the changed behavior and required project gates.
  Once they pass, expand or repeat them only for new changes, failures, or
  unresolved concerns. Documentation-only work normally needs diff/link checks.
- Report what changed, verification performed, and material limitations.
  Distinguish an observation from an inference and unrun checks from passing ones.
  If a policy blocks requested work, identify its file/section and the blocked
  action; continue independent authorized work rather than stopping everything.
- Prefer concise, connected prose; use lists for genuinely parallel information.
  Do not narrate every command or repeat the plan in each update.
