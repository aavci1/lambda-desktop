// Multiline TextInput `.styler` demo: lightweight markdown-inspired highlighting (headings, bold, `code`).

#include <Lambda.hpp>
#include <Lambda/Graphics/AttributedString.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/TextInput.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <map>
#include <string>
#include <vector>

using namespace lambdaui;

namespace {

enum class Elements {
    h1,
    h2,
    h3,
    body,
    code
};

/// Maps the full UTF-8 buffer to non-overlapping attributed runs (required by multiline \c TextInput).
std::vector<AttributedRun> markdownStyler(std::string_view sv) {
    std::string const text(sv);
    std::uint32_t const n = static_cast<std::uint32_t>(text.size());
    std::vector<AttributedRun> runs;
    if (n == 0) {
        return runs;
    }

    auto baseFont = Font {.family = "Menlo", .size = 14.f, .weight = 400.f};
    auto fonts = std::map<Elements, Font> {};
    fonts[Elements::h1] = Font {.size = baseFont.size * 2.0f, .weight = baseFont.weight * 1.25f};
    fonts[Elements::h2] = Font {.size = baseFont.size * 1.6f, .weight = baseFont.weight * 1.25f};
    fonts[Elements::h3] = Font {.size = baseFont.size * 1.3f, .weight = baseFont.weight * 1.25f};
    fonts[Elements::body] = baseFont;
    fonts[Elements::code] = baseFont;

    auto baseColor = Color::hex(0xC2C2C2);
    auto colors = std::map<Elements, Color> {};
    colors[Elements::h1] = baseColor;
    colors[Elements::h2] = baseColor;
    colors[Elements::h3] = baseColor;
    colors[Elements::body] = baseColor;
    colors[Elements::code] = baseColor;

    Font headingFont = resolveFont(Font::theme(), fonts[Elements::h1]);
    Font codeFont = resolveFont(Font::theme(), fonts[Elements::code]);
    Font boldFont = baseFont;
    boldFont.weight = (boldFont.weight > 0.f ? boldFont.weight : 400.f) + 250.f;
    if (boldFont.weight > 900.f) {
        boldFont.weight = 900.f;
    }

    auto push = [&](std::uint32_t a, std::uint32_t b, Font const &f, Color c) {
        if (a < b) {
            runs.push_back(AttributedRun {a, b, f, c});
        }
    };

    auto parseInline = [&](std::uint32_t lineStart, std::uint32_t lineEnd) {
        std::uint32_t p = lineStart;
        while (p < lineEnd) {
            if (p + 1 < lineEnd && text[static_cast<std::size_t>(p)] == '*' &&
                text[static_cast<std::size_t>(p + 1)] == '*') {
                std::uint32_t q = p + 2;
                while (q + 1 < lineEnd) {
                    if (text[static_cast<std::size_t>(q)] == '*' && text[static_cast<std::size_t>(q + 1)] == '*') {
                        break;
                    }
                    ++q;
                }
                if (q + 1 < lineEnd && text[q] == '*' && text[q + 1] == '*') {
                    push(p, p + 2, baseFont, baseColor);
                    push(p + 2, q, boldFont, baseColor);
                    push(q, q + 2, baseFont, baseColor);
                    p = q + 2;
                    continue;
                }
            }
            if (text[static_cast<std::size_t>(p)] == '`') {
                std::uint32_t q = p + 1;
                while (q < lineEnd && text[static_cast<std::size_t>(q)] != '`') {
                    ++q;
                }
                if (q < lineEnd) {
                    push(p, p + 1, baseFont, baseColor);
                    push(p + 1, q, codeFont, colors[Elements::code]);
                    push(q, q + 1, baseFont, baseColor);
                    p = q + 1;
                    continue;
                }
            }
            std::uint32_t const start = p;
            ++p;
            while (p < lineEnd) {
                char const ch = text[static_cast<std::size_t>(p)];
                if (ch == '`' || (p + 1 < lineEnd && ch == '*' && text[static_cast<std::size_t>(p + 1)] == '*')) {
                    break;
                }
                ++p;
            }
            push(start, p, baseFont, baseColor);
        }
    };

    auto processLine = [&](std::uint32_t ls, std::uint32_t le) {
        if (ls >= le) {
            return;
        }
        std::uint32_t contentEnd = le;
        bool const endsNl = contentEnd > ls && text[static_cast<std::size_t>(contentEnd - 1)] == '\n';
        if (endsNl) {
            --contentEnd;
        }

        if (contentEnd <= ls) {
            push(ls, le, baseFont, baseColor);
            return;
        }

        std::uint32_t i = ls;
        int hashes = 0;
        while (i < contentEnd && hashes < 3 && text[static_cast<std::size_t>(i)] == '#') {
            ++hashes;
            ++i;
        }
        if (hashes > 0 && i < contentEnd && text[static_cast<std::size_t>(i)] == ' ') {
            ++i;
            Font f = fonts[Elements::h1];
            Color c = colors[Elements::h1];
            if (hashes >= 2) {
                f = fonts[Elements::h2];
                c = colors[Elements::h2];
            }
            if (hashes >= 3) {
                f = fonts[Elements::h3];
                c = colors[Elements::h3];
            }
            push(ls, contentEnd, f, c);
            if (endsNl) {
                push(contentEnd, le, baseFont, baseColor);
            }
            return;
        }

        std::uint32_t j = ls;
        while (j < contentEnd && text[static_cast<std::size_t>(j)] == ' ') {
            ++j;
        }
        if (j < contentEnd && text[static_cast<std::size_t>(j)] == '-' && j + 1 < contentEnd &&
            text[static_cast<std::size_t>(j + 1)] == ' ') {
            push(ls, j + 2, baseFont, baseColor);
            parseInline(j + 2, contentEnd);
            if (endsNl) {
                push(contentEnd, le, baseFont, baseColor);
            }
            return;
        }

        parseInline(ls, contentEnd);
        if (endsNl) {
            push(contentEnd, le, baseFont, baseColor);
        }
    };

    std::uint32_t ls = 0;
    while (ls < n) {
        std::uint32_t le = ls;
        while (le < n) {
            if (text[static_cast<std::size_t>(le)] == '\n') {
                break;
            }
            le++;
        }
        processLine(ls, le + 1);
        ls = le + 1;
    }

    return runs;
}

} // namespace

struct MarkdownEditor {
    auto body() const {
        auto doc = useState(std::string {
            R"(# Lambda Layout System

## Architecture Overview

Lambda uses a **retained scene graph** model. There is no base `View` class — views are plain structs that satisfy one of three C++ concepts. A universal wrapper called `Element` type-erases them and dispatches measurement and mounting through a virtual `Concept`/`Model<C>` pattern, then retained nodes are relaid out in place as constraints and reactive bindings change.

The pipeline for each rebuild has two distinct phases:

```
Phase 1: Mount + Relayout     — Element tree → retained SceneGraph
Phase 2: Paint                — Retained SceneGraph → Canvas
```

**Phase 1** walks the element tree once, runs `measure` and flex distribution, records assigned geometry in scene nodes and the geometry index, and keeps retained nodes alive across reactive updates. The only dependencies are `LayoutConstraints`, `LayoutHints`, and `TextSystem` (for text measurement).

**Phase 2** walks the retained `SceneGraph` to Canvas. Paint retention is node-local, and input/hit testing also resolves directly against retained scene nodes rather than a separate event map.

)"
        });

        return VStack {
            .spacing = 12.f,
            .children =
                children(
                    Text {
                        .text = "Markdown styler (multiline TextInput)",
                        .font = Font::largeTitle(),
                        .color = Color::primary(),
                    },
                    Text {
                        .text = "The `.styler` field receives the full document and returns AttributedRun ranges.",
                        .font = Font::body(),
                        .color = Color::secondary(),
                        .wrapping = TextWrapping::Wrap,
                    },
                    TextInput {
                        .value = doc,
                        .placeholder = "Write markdown-like text…",
                        .styler = markdownStyler,
                        .style = {
                            .backgroundColor = Color::hex(0x1F1F1F),
                        },
                        .multiline = true,
                    }
                        .flex(1.f, 1.f, 0.f)
                ),
        }
            .padding(20.f);
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);
    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda — Markdown styler",
        .resizable = true,
    });

    w.setView<MarkdownEditor>();
    return app.exec();
}
