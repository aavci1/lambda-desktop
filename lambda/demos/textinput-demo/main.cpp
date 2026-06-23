// Unified TextInput demo: single-line and multiline modes in one example.

#include <Lambda.hpp>
#include <Lambda/UI/Action.hpp>
#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/TextInput.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <cstdio>
#include <string>
#include <string_view>

using namespace lambdaui;

namespace {

Element sectionCard(Theme const &theme, std::string title, std::string body, Element content) {
    return Card {
        .child = VStack {
            .spacing = theme.space3,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = std::move(title),
                    .font = Font::title2(),
                    .color = Color::primary(),
                },
                Text {
                    .text = std::move(body),
                    .font = Font::footnote(),
                    .color = Color::secondary(),
                    .wrapping = TextWrapping::Wrap,
                },
                std::move(content)
            )
        },
        .style = Card::Style {
            .padding = theme.space4,
            .cornerRadius = theme.radiusLarge,
        },
    };
}

Element labeledField(Theme const &theme, std::string label, Element field) {
    return VStack {
        .spacing = theme.space2,
        .alignment = Alignment::Start,
        .children = children(
            Text {
                .text = std::move(label),
                .font = Font::headline(),
                .color = Color::primary(),
            },
            std::move(field)
        )
    };
}

} // namespace

struct TextInputShowcase {
    auto body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto name = useState(std::string {"Abdurrahman Avci"});
        auto email = useState(std::string {"hello@lambda.dev"});
        auto search = useState(std::string {});
        auto bio = useState(std::string {
            "Lambda now uses one TextInput component for both single-line and multiline editing.\n\n"
            "Try clicking, selecting, scrolling, and using the normal edit shortcuts."
        });
        auto draft = useState(std::string {
            "Daily notes\n"
            "- Review the unified text input stack\n"
            "- Check caret visibility while scrolling\n"
            "- Validate focus and selection behavior"
        });
        auto disabled = useState(std::string {"Disabled example"});

        Color const okColor = Color::primary();
        Color const badColor = Color::danger();

        return VStack {
            .spacing = theme().space4,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = "TextInput",
                    .font = Font::largeTitle(),
                    .color = Color::primary(),
                },
                Text {
                    .text =
                        "One control, two modes. This example covers submit-driven single-line fields, "
                        "validation, plain styling, multiline editing, and disabled states.",
                    .font = Font::body(),
                    .color = Color::secondary(),
                    .wrapping = TextWrapping::Wrap,
                },
                HStack {
                    .spacing = theme().space4,
                    .alignment = Alignment::Start,
                    .children = children(
                        sectionCard(
                            theme(), "Single-Line",
                            "Tab between fields, press Return to submit, and use the normal clipboard shortcuts.",
                            VStack {
                                .spacing = theme().space3,
                                .children = children(
                                    labeledField(
                                        theme(), "Name",
                                        TextInput {
                                            .value = name,
                                            .placeholder = "Your name",
                                            .onSubmit = [](std::string const &v) {
                                                std::fprintf(stderr, "[textinput-demo] submit name: %s\n", v.c_str());
                                            },
                                        }
                                    ),
                                    labeledField(
                                        theme(),
                                        "Email",
                                        TextInput {
                                            .value = email,
                                            .placeholder = "you@example.com",
                                            .validationColor = [okColor, badColor](std::string_view v) {
                                                return v.find('@') == std::string_view::npos ? badColor : okColor;
                                            },
                                            .onSubmit = [](std::string const &v) {
                                                std::fprintf(stderr, "[textinput-demo] submit email: %s\n", v.c_str());
                                            },
                                        }
                                    ),
                                    labeledField(
                                        theme(),
                                        "Search",
                                        TextInput {
                                            .value = search,
                                            .placeholder = "Search…",
                                            .style = TextInput::Style::plain(),
                                        }
                                            .padding(theme().space3)
                                            .fill(FillStyle::solid(Color::controlBackground()))
                                            .stroke(StrokeStyle::solid(Color::opaqueSeparator(), 1.f))
                                            .cornerRadius(CornerRadius {theme().radiusMedium})
                                    ),
                                    labeledField(
                                        theme(),
                                        "Disabled",
                                        TextInput {
                                            .value = disabled,
                                            .placeholder = "N/A",
                                            .disabled = true,
                                        }
                                    )
                                )
                            }
                        )
                            .flex(1.f, 1.f, 0.f),
                        sectionCard(
                            theme(),
                            "Multiline",
                            "Multiline mode scrolls inside the field, keeps the caret visible, and uses the same editing model.",
                            VStack {
                                .spacing = theme().space3,
                                .children = children(
                                    labeledField(
                                        theme(),
                                        "Profile Bio",
                                        TextInput {
                                            .value = bio,
                                            .placeholder = "Tell us about yourself…",
                                            .multiline = true,
                                            .multilineHeight = {.fixed = 180.f},
                                        }
                                    ),
                                    labeledField(
                                        theme(),
                                        "Working Draft",
                                        TextInput {
                                            .value = draft,
                                            .placeholder = "Write longer notes…",
                                            .multiline = true,
                                            .multilineHeight = {.fixed = 220.f},
                                            .onEscape = [draft](std::string const &) {
                                                draft = "";
                                            },
                                        }
                                    ),
                                    labeledField(
                                        theme(),
                                        "Disabled Multiline",
                                        TextInput {
                                            .value = disabled,
                                            .placeholder = "N/A",
                                            .multiline = true,
                                            .disabled = true,
                                            .multilineHeight = {.fixed = 120.f},
                                        }
                                    )
                                )
                            }
                        )
                            .flex(1.f, 1.f, 0.f)
                    )
                }
                    .flex(1.f, 1.f, 0.f)
            )
        }
            .padding(24.f);
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);
    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda — TextInput",
        .resizable = true,
    });

    w.registerAction("edit.copy", {.label = "Copy", .shortcut = shortcuts::Copy});
    w.registerAction("edit.cut", {.label = "Cut", .shortcut = shortcuts::Cut});
    w.registerAction("edit.paste", {.label = "Paste", .shortcut = shortcuts::Paste});
    w.registerAction("edit.selectAll", {.label = "Select All", .shortcut = shortcuts::SelectAll});
    w.registerAction("app.quit", {.label = "Quit", .shortcut = shortcuts::Quit, .isEnabled = [] { return true; }});

    w.setView<TextInputShowcase>();
    return app.exec();
}
