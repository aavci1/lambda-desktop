# Flux documentation

## Start here

| Document | Contents |
|----------|----------|
| [roadmap.md](roadmap.md) | **Project status, active backlog (P0–P3), archived milestones** |
| [conventions.md](conventions.md) | Repository layout, CMake, namespaces, platforms, examples |

## Flux v5 (application framework)

| Document | Contents |
|----------|----------|
| [reactive-graph.md](reactive-graph.md) | Signals, computed values, effects, scopes, bindings |
| [composites.md](composites.md) | Retained components and mount semantics |
| [migrating-to-v5.md](migrating-to-v5.md) | Practical migration notes for apps and examples |
| [ui-view-body-style.md](ui-view-body-style.md) | Formatting conventions for `body()` trees |
| [event_queue.md](event_queue.md) | `EventQueue` API, dispatch order, threading |

## Linux compositor and shell

| Document | Contents |
|----------|----------|
| [compositor.md](compositor.md) | Compositor architecture, phases, framework boundary, change log |
| [lambda-desktop-assessment.md](lambda-desktop-assessment.md) | Source-of-truth readiness index for Lambda desktop specs and remaining unspecced areas |
| [lambda-window-manager-readiness-spec.md](lambda-window-manager-readiness-spec.md) | Detailed Window Manager hardening spec for the first daily-driver milestone |
| [lambda-shell-spec.md](lambda-shell-spec.md) | Shell vs window-manager boundary, surfaces, IPC |
| [lambda-shell-readiness-spec.md](lambda-shell-readiness-spec.md) | Detailed Shell readiness spec for app discovery, dock, launcher, top bar, and icons |
| [lambda-shell-status-bar-plan.md](lambda-shell-status-bar-plan.md) | Investigation and implementation plan for real top-bar status providers and quick settings |
| [lambda-settings-readiness-spec.md](lambda-settings-readiness-spec.md) | Detailed Settings readiness spec for real config editing and system status |
| [lambda-files-readiness-spec.md](lambda-files-readiness-spec.md) | Detailed Files readiness spec for safe file operations, trash, selection, and open-with |
| [lambda-terminal-readiness-spec.md](lambda-terminal-readiness-spec.md) | Detailed Terminal readiness spec for scrollback, clipboard, keys, Unicode, and performance |
| [compositor-user-guide.md](compositor-user-guide.md) | Build, configure, run on a TTY |
| [linux-development.md](linux-development.md) | Linux package setup and build notes |

The project root [README.md](../README.md) lists build commands and example targets.
