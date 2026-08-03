# dmf-mf-mxl-compositor - working rules

Builds one container image carrying the MXL mosaic compositor and its
companion binaries. Nothing else ships from here.

## STOP. Use a git worktree.

Before any mutation -- edit, write, commit, branch create, push, `gh pr
create` -- set up a dedicated worktree first. No change is small enough to
skip it: typos, one-line fixes and edits to this file all require it.
Multiple sessions work this tree in parallel, and two writers in one checkout
corrupt staging state and lose work.

Worktrees live under `<repo>/.claude/worktrees/`, nowhere else.

    git fetch origin
    id=$(openssl rand -hex 4)
    git worktree add .claude/worktrees/<topic>-$id -b <topic> origin/main
    cd .claude/worktrees/<topic>-$id

The main checkout is read-only: `git log`, `git diff`, `git status`, grep,
find, reading files. Anything touching the index, working tree, branch list or
remote happens in the worktree.

## Boundaries

- This repository builds an image. The Helm chart that deploys it and the
  `MediaFunctionClass` that registers it live in `dmf-catalog`. Never add a
  chart, a manifest or a Kubernetes object here.
- No deployment topology in this tree. No cluster names, addresses,
  namespaces, or assumptions about who consumes the image. Describe what a
  binary does and what it needs; the downstream choice is not this
  repository's to characterise.
- This repository is public. No internal environment detail, no ticket
  references, no private repository names, no notes left over from a working
  session.

## go-mxl lock-step

`ARG GO_MXL_TAG` selects both base images and is the only version knob. MXL's
domain protocol requires every reader and writer sharing a domain to load a
byte-identical `libmxl.so`; a mismatch surfaces as `MXL_ERR_UNKNOWN` from
`mxlCreateFlowReader`, or as grains that read as garbage. Bumping it here
without bumping the gateway and node agent reading the same domain breaks
them. Renovate proposes the bump; accepting it is a cross-repository decision.

`vendor/mxl/` holds MXL headers from an external source. Preserve their SPDX
lines. `libmxl.so` itself comes from the base image.

## Voice

- Terse. Say the thing, stop. No preamble, no recap, no restating the task.
- No filler adjectives (robust, seamless, powerful, comprehensive). State what
  the code does, not how good it is.
- Comments explain *why*, not *what*. Delete comments that restate the code.
  Do not narrate what was tried first or what failed.
- Treat comments and docs as a whole rather than appending to them. Revalidate
  against the code and remove anything stale.
- Write declarative facts. No personal pronouns, no addressing a reader.
- ASCII only: `-`, `--`, `"`, `'`, `->`, `...`. No em-dash, no typographic
  quotes, no arrows, no emoji. Applies to code, comments, docs, commit
  messages and PR descriptions.

## Commits

Conventional commits, imperative, subject <= 72 chars, body wrapped at 72. The
type drives the version bump and the changelog section. Scope names the binary
where one applies (`fix(audio-preview): ...`). Breaking changes take `!` or a
`BREAKING CHANGE:` footer.

Explain why the change exists; the diff already shows what. No ticket numbers.
No `Co-Authored-By` trailers. No checklists or "Summary" sections.

## Releasing

release-please, manifest mode, one package for the repository, `1.0.0-rc.N`
prereleases tagged `vX.Y.Z`. Publishing the release triggers the image build.
`RELEASE_PLEASE_TOKEN` is load-bearing: a release published with the default
`GITHUB_TOKEN` starts no further workflow and no version-tagged image appears.

## Before finishing

The image is the deliverable, so verify it builds. A change to the sources or
the Dockerfile is not done until `docker build` succeeds.
