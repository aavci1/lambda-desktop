# Lambda documentation

## Start here

| Document | Contents |
|----------|----------|
| [conventions.md](conventions.md) | Repository layout, CMake, namespaces, platforms, examples |
| [lambda/docs](../lambda/docs/) | Framework reference docs |
| [lambda-desktop/docs](../lambda-desktop/docs/) | Linux desktop roadmap, compositor docs, and runbooks |

## Lambda v5 (application framework)

| Document | Contents |
|----------|----------|
| [reactive-graph.md](../lambda/docs/reactive-graph.md) | Signals, computed values, effects, scopes, bindings |
| [composites.md](../lambda/docs/composites.md) | Retained components and mount semantics |
| [migrating-to-v5.md](../lambda/docs/migrating-to-v5.md) | Practical migration notes for apps and examples |
| [ui-view-body-style.md](../lambda/docs/ui-view-body-style.md) | Formatting conventions for `body()` trees |
| [event_queue.md](../lambda/docs/event_queue.md) | `EventQueue` API, dispatch order, threading |

## Linux compositor and shell

| Document | Contents |
|----------|----------|
| [roadmap.md](../lambda-desktop/docs/roadmap.md) | Source of truth for Lambda desktop status, ownership boundaries, active readiness gates, and deferred work |
| [lambda-settings-ux-plan.md](../lambda-desktop/docs/lambda-settings-ux-plan.md) | Active UX and organization plan for `lambda-settings` |
| [compositor.md](../lambda-desktop/docs/compositor.md) | Compositor architecture/history reference, phases, framework boundary, change log |
| [compositor-wlroots-improvement-plan.md](../lambda-desktop/docs/compositor-wlroots-improvement-plan.md) | Active wlroots comparison plan for compositor protocol, state, rendering, and validation improvements |
| [compositor-user-guide.md](../lambda-desktop/docs/compositor-user-guide.md) | Build, configure, run on a TTY |
| [linux-development.md](../lambda-desktop/docs/linux-development.md) | Linux package setup and build notes |

The project root [README.md](../README.md) lists build commands and example targets.
