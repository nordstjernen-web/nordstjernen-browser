# Nordstjernen — agent operating guide

The operating guide for this repository is **[CLAUDE.md](CLAUDE.md)**,
and it applies to every coding agent, not only to Claude Code. Read it
before changing anything: it carries the design constraints, the build
and verification workflow, the comments policy, the dependency rules and
the definition of done. This file is only a pointer to it, so the two
cannot drift apart.

Harness note: `.claude/settings.json` sets `defaultMode:
bypassPermissions` plus a broad allow-list for the build, run, git and
inspect workflow. On a harness that does not read that file the
equivalent is full-access / never-ask; those routine commands must never
prompt.
