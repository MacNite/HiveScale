# Project instructions for Claude Code

## Delivery policy — DO NOT PUSH; hand me a downloadable zip instead

This repository uses a **"no remote writes"** workflow. When you finish making
changes, **do not publish them to GitHub** — package them into a zip and give it
to me to download.

**You MUST NOT, under any circumstances unless I explicitly ask in that very message:**

- run `git push` (to any branch or remote);
- create, update, merge, or comment on pull requests;
- create or move remote branches or tags;
- push files through the GitHub API / MCP tools (`create_or_update_file`,
  `push_files`, `create_pull_request`, etc.);
- otherwise transmit repository contents to GitHub or any external service.

Working **locally** is fine: edit files, run builds/tests, and commit to the
local branch if it helps you organize work. Just never send anything to the
remote.

### What to do instead — at the end of a task

1. Make your changes in the working tree as usual.
2. Bundle the modified files into a zip.

3. **Surface that zip to me as a downloadable file** so I can grab it. In
   Claude Code on the web, send the file to me directly so it shows up as a
   download (do not just print the path). When running locally, the zip is
   already in `claude-bundles/` for me to open.
4. In your final message, list what changed and what the zip contains, and
   confirm that **nothing was pushed**.

### If I explicitly ask you to push

Only then may you push — and confirm the exact branch with me first. The
default, every other time, is the zip.

---

## About this project

HiveScale is an ESP32-based dual beehive scale system with a self-hosted
FastAPI + PostgreSQL backend.

- `firmware/` — ESP32 PlatformIO project (`src/main.cpp` is the main source).
- `server/` — Python FastAPI backend and insights logic.
- `docker/` — Docker Compose deployment for the API and database.
- `pcb-design/` — KiCad breakout PCB design and fabrication outputs.
- `docs/` — hardware, API, deployment, and test documentation.
- `test-data/` — mock server and sample payloads.

Secrets live in `.env` / `secrets.h` files that are gitignored — never add real
credentials to tracked files or to a bundle you hand back.
