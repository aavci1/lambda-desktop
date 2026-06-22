#include <Lambda.hpp>
#include <Lambda/UI/Action.hpp>
#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/Grid.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Show.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <cstdio>
#include <string>

using namespace lambda;

namespace {

Element makeSectionCard(Theme const &theme, std::string title, std::string caption, Element content) {
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
                    .text = std::move(caption),
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

Element makeBadge(Theme const &theme, Bindable<std::string> label,
                  Bindable<Color> fill, Color textColor) {
    return Text {
        .text = std::move(label),
        .font = Font::caption(),
        .color = textColor,
        .horizontalAlignment = HorizontalAlignment::Center,
        .verticalAlignment = VerticalAlignment::Center,
    }
        .fill(std::move(fill))
        .cornerRadius(CornerRadius {theme.radiusLarge})
        .padding(6.f, 10.f, 6.f, 10.f);
}

Element makeMetricTile(Theme const &theme, Bindable<std::string> value,
                       std::string label, Color accent) {
    return VStack {
        .spacing = theme.space1,
        .alignment = Alignment::Start,
        .children = children(
            Text {
                .text = std::move(value),
                .font = Font::title2(),
                .color = accent,
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Text {
                .text = std::move(label),
                .font = Font::footnote(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            }
        )
    } //
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium})
        .flex(1.f, 1.f, 0.f);
}

Element makeHeroDemo(Theme const &theme, Signal<bool> dirty, Signal<bool> reviewPassed, Signal<int> saveCount,
                     Signal<std::string> lastEvent) {
    auto saveDraft = [dirty, saveCount, lastEvent] {
        if (!*dirty) {
            return;
        }
        dirty = false;
        saveCount = *saveCount + 1;
        lastEvent = "Draft saved. Cmd+S is wired to the same action.";
        std::fprintf(stderr, "[button-demo] Save draft\n");
    };

    auto markDirty = [dirty, lastEvent] {
        dirty = true;
        lastEvent = "Draft marked dirty again to show the disabled and enabled transitions.";
        std::fprintf(stderr, "[button-demo] Mark dirty\n");
    };
    auto heroLinkAction = [dirty, saveDraft, markDirty] {
        if (dirty()) {
            saveDraft();
        } else {
            markDirty();
        }
    };
    auto heroStatusRow = [heroLinkAction](std::string prompt, std::string actionLabel) {
        return HStack {
            .spacing = 4.f,
            .alignment = Alignment::Center,
            .children = children(
                Text {
                    .text = std::move(prompt),
                    .font = Font::footnote(),
                    .color = Color::secondary(),
                },
                LinkButton {
                    .label = std::move(actionLabel),
                    .style = LinkButton::Style {.font = Font::footnote()},
                    .onTap = heroLinkAction,
                }
            )
        };
    };

    return makeSectionCard(
        theme, "Editorial Hero",
        "A primary action should feel obvious, while the supporting actions stay nearby without competing for attention.",
        VStack {
            .spacing = theme.space3,
            .children = children(
                HStack {
                    .spacing = theme.space3,
                    .alignment = Alignment::Center,
                    .children = children(
                        VStack {
                            .spacing = theme.space1,
                            .alignment = Alignment::Start,
                            .children = children(
                                Text {
                                    .text = "Spring release brief",
                                    .font = Font::title3(),
                                    .color = Color::primary(),
                                    .horizontalAlignment = HorizontalAlignment::Leading,
                                },
                                Text {
                                    .text = "Buttons feel strongest when the layout makes one decision unmistakably primary.",
                                    .font = Font::footnote(),
                                    .color = Color::secondary(),
                                    .horizontalAlignment = HorizontalAlignment::Leading,
                                    .wrapping = TextWrapping::Wrap,
                                }
                            )
                        } //
                            .flex(1.f, 1.f, 0.f),
                        makeBadge(theme,
                                  [dirty] {
                                      return dirty() ? std::string {"Dirty"} : std::string {"Saved"};
                                  },
                                  [dirty] {
                                      return dirty() ? Color::warningBackground() : Color::successBackground();
                                  },
                                  Color::primary()),
                        makeBadge(theme,
                                  [reviewPassed] {
                                      return reviewPassed() ? std::string {"Approved"} : std::string {"Needs review"};
                                  },
                                  [reviewPassed] {
                                      return reviewPassed() ? Color::successBackground() : Color::selectedContentBackground();
                                  },
                                  Color::primary())
                    )
                },
                VStack {
                    .spacing = theme.space2,
                    .children = children(
                        HStack {
                            .spacing = theme.space2,
                            .alignment = Alignment::Center,
                            .children = children(
                                Button {
                                    .label = "Save Draft",
                                    .variant = ButtonVariant::Primary,
                                    .disabled = [dirty] {
                                        return !dirty();
                                    },
                                    .onTap = saveDraft,
                                },
                                Button {
                                    .label = "Preview",
                                    .variant = ButtonVariant::Secondary,
                                    .onTap =
                                        [lastEvent] {
                                            lastEvent = "Preview opened. Secondary buttons are useful when they support the main flow.";
                                            std::fprintf(stderr, "[button-demo] Preview\n");
                                        },
                                },
                                Button {
                                    .label = "Share Review Link",
                                    .variant = ButtonVariant::Ghost,
                                    .onTap =
                                        [reviewPassed, lastEvent] {
                                            reviewPassed = true;
                                            lastEvent = "Review link shared. The draft is now marked approved.";
                                            std::fprintf(stderr, "[button-demo] Share review link\n");
                                        },
                                }
                            )
                        },
                        Show(
                            dirty,
                            [heroStatusRow] {
                                return heroStatusRow("Need to show the saved state?", "Save with Cmd+S");
                            },
                            [heroStatusRow] {
                                return heroStatusRow("Need to bring the dirty state back?",
                                                     "Mark this draft dirty again");
                            })
                    )
                } //
                    .padding(theme.space3)
                    .fill(FillStyle::solid(Color::windowBackground()))
                    .cornerRadius(CornerRadius {theme.radiusMedium}),
                Text {
                    .text = [lastEvent] {
                        return lastEvent();
                    },
                    .font = Font::footnote(),
                    .color = Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                    .wrapping = TextWrapping::Wrap,
                }
            )
        }
    );
}

Element makeVariantTile(Theme const &theme, std::string title, std::string caption, Element action) {
    return VStack {
        .spacing = theme.space2,
        .alignment = Alignment::Stretch,
        .children = children(
            Text {
                .text = std::move(title),
                .font = Font::title3(),
                .color = Color::primary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Text {
                .text = std::move(caption),
                .font = Font::footnote(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
                .wrapping = TextWrapping::Wrap,
            },
            Spacer {},
            std::move(action)
        )
    } //
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium})
        .height(184.f)
        .flex(1.f, 1.f, 0.f);
}

Element makeVariantGallery(Theme const &theme, Signal<std::string> lastEvent) {
    return makeSectionCard(
        theme, "Variants In Context",
        "Each button style gets a believable job: primary commits, secondary supports, ghost stays quiet, and destructive asks for intent.",
        Grid {
            .columns = 2,
            .horizontalSpacing = theme.space3,
            .verticalSpacing = theme.space3,
            .children = children(
                makeVariantTile(
                    theme, "Primary",
                    "Use one per cluster when the screen has a clear next step.",
                    Button {
                        .label = "Create Invoice",
                        .variant = ButtonVariant::Primary,
                        .onTap =
                            [lastEvent] {
                                lastEvent = "Primary pressed: a single obvious action works best when it carries the most visual weight.";
                                std::fprintf(stderr, "[button-demo] Create invoice\n");
                            },
                    }
                ),
                makeVariantTile(
                    theme, "Secondary",
                    "Supportive tasks can stay prominent without overpowering the primary action.",
                    Button {
                        .label = "Compare Plans",
                        .variant = ButtonVariant::Secondary,
                        .onTap =
                            [lastEvent] {
                                lastEvent = "Secondary pressed: supportive actions should remain easy to find but visually lighter.";
                                std::fprintf(stderr, "[button-demo] Compare plans\n");
                            },
                    }
                ),
                makeVariantTile(
                    theme, "Destructive",
                    "Reserve this for irreversible or expensive actions so it keeps its signal.",
                    Button {
                        .label = "Delete Workspace",
                        .variant = ButtonVariant::Destructive,
                        .onTap =
                            [lastEvent] {
                                lastEvent = "Destructive pressed: this variant should feel deliberate, not routine.";
                                std::fprintf(stderr, "[button-demo] Delete workspace\n");
                            },
                    }
                ),
                makeVariantTile(
                    theme, "Ghost",
                    "Low-chrome buttons fit quiet utility rows, filters, and lightweight dismissal actions.",
                    Button {
                        .label = "Skip For Now",
                        .variant = ButtonVariant::Ghost,
                        .onTap =
                            [lastEvent] {
                                lastEvent = "Ghost pressed: subtle actions still need a clear hover and focus state.";
                                std::fprintf(stderr, "[button-demo] Skip for now\n");
                            },
                    }
                )
            )
        }
    );
}

Element makeToolbarDemo(Theme const &theme, Signal<bool> dirty, Signal<bool> reviewPassed, Signal<int> saveCount,
                        Signal<int> publishCount, Signal<std::string> lastEvent) {
    auto saveDraft = [dirty, saveCount, lastEvent] {
        if (!*dirty) {
            return;
        }
        dirty = false;
        saveCount = *saveCount + 1;
        lastEvent = "Toolbar save completed. Keyboard and button paths stay aligned.";
        std::fprintf(stderr, "[button-demo] Toolbar save\n");
    };

    return makeSectionCard(
        theme, "Toolbar Composition",
        "Dense layouts need hierarchy too. Ghost buttons handle utility actions, while the trailing save and publish buttons carry the stronger affordances.",
        VStack {
            .spacing = theme.space3,
            .children = children(
                HStack {
                    .spacing = theme.space2,
                    .alignment = Alignment::Center,
                    .children = children(
                        Button {
                            .label = "Back",
                            .variant = ButtonVariant::Ghost,
                            .onTap =
                                [lastEvent] {
                                    lastEvent = "Back pressed. Ghost buttons are useful for low-risk navigation inside dense toolbars.";
                                    std::fprintf(stderr, "[button-demo] Back\n");
                                },
                        },
                        Button {
                            .label = "Comments",
                            .variant = ButtonVariant::Ghost,
                            .onTap =
                                [lastEvent] {
                                    lastEvent = "Comments opened.";
                                    std::fprintf(stderr, "[button-demo] Comments\n");
                                },
                        },
                        Button {
                            .label = "History",
                            .variant = ButtonVariant::Ghost,
                            .onTap =
                                [lastEvent] {
                                    lastEvent = "Version history opened.";
                                    std::fprintf(stderr, "[button-demo] History\n");
                                },
                        },
                        Spacer {},
                        Button {
                            .label = [reviewPassed] {
                                return reviewPassed() ? std::string {"Reset Review"} : std::string {"Request Review"};
                            },
                            .variant = ButtonVariant::Secondary,
                            .onTap =
                                [reviewPassed, lastEvent] {
                                    reviewPassed = !*reviewPassed;
                                    lastEvent = *reviewPassed ? "Review requested and marked ready." : "Review reset. Publish is disabled again.";
                                    std::fprintf(stderr, "[button-demo] Toggle review\n");
                                },
                        },
                        Button {
                            .label = "Save",
                            .variant = ButtonVariant::Primary,
                            .disabled = [dirty] {
                                return !dirty();
                            },
                            .onTap = saveDraft,
                        },
                        Button {
                            .label = "Publish",
                            .variant = ButtonVariant::Primary,
                            .disabled = [dirty, reviewPassed] {
                                return dirty() || !reviewPassed();
                            },
                            .onTap =
                                [dirty, reviewPassed, publishCount, lastEvent] {
                                    if (*dirty || !*reviewPassed) {
                                        return;
                                    }
                                    publishCount = *publishCount + 1;
                                    lastEvent = "Published successfully. The primary action becomes available only when the workflow is truly ready.";
                                    std::fprintf(stderr, "[button-demo] Publish\n");
                                },
                        }
                    )
                } //
                    .padding(theme.space3)
                    .fill(FillStyle::solid(Color::windowBackground()))
                    .cornerRadius(CornerRadius {theme.radiusMedium}),
                HStack {
                    .spacing = theme.space3,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        makeMetricTile(theme, [saveCount] {
                            return std::to_string(saveCount());
                        }, "Saves", Color::accent()),
                        makeMetricTile(theme, [publishCount] {
                            return std::to_string(publishCount());
                        }, "Publishes", Color::success()),
                        makeMetricTile(theme, [reviewPassed] {
                            return reviewPassed() ? std::string {"Ready"} : std::string {"Blocked"};
                        }, "Review status", Color::warning())
                    )
                }
            )
        }
    );
}

Element makeInlineDemo(Theme const &theme, Signal<bool> reviewPassed, Signal<std::string> lastEvent) {
    return makeSectionCard(
        theme, "Inline Actions",
        "Links should read like part of the sentence, not like disguised boxed buttons. They work well for help, secondary disclosure, and low-friction follow-up actions.",
        VStack {
            .spacing = theme.space3,
            .alignment = Alignment::Start,
            .children = children(
                HStack {
                    .spacing = 4.f,
                    .alignment = Alignment::Center,
                    .children = children(
                        Text {
                            .text = "Need copy guidance before publishing?",
                            .font = Font::body(),
                            .color = Color::primary(),
                            .wrapping = TextWrapping::Wrap,
                        },
                        LinkButton {
                            .label = "Open the editorial checklist",
                            .style = LinkButton::Style {.font = Font::body()},
                            .onTap =
                                [lastEvent] {
                                    lastEvent = "Checklist opened from an inline link.";
                                    std::fprintf(stderr, "[button-demo] Open checklist\n");
                                },
                        }
                    )
                },
                HStack {
                    .spacing = 4.f,
                    .alignment = Alignment::Center,
                    .children = children(
                        Text {
                            .text = [reviewPassed] {
                                return reviewPassed()
                                           ? std::string {"Review already passed."}
                                           : std::string {"Still waiting on approval?"};
                            },
                            .font = Font::footnote(),
                            .color = Color::secondary(),
                        },
                        LinkButton {
                            .label = [reviewPassed] {
                                return reviewPassed()
                                           ? std::string {"Mark review as pending"}
                                           : std::string {"Mark review as approved"};
                            },
                            .style = LinkButton::Style {.font = Font::footnote()},
                            .onTap =
                                [reviewPassed, lastEvent] {
                                    reviewPassed = !*reviewPassed;
                                    lastEvent = *reviewPassed ? "Review approved from an inline link." : "Review set back to pending from an inline link.";
                                    std::fprintf(stderr, "[button-demo] Toggle inline review\n");
                                },
                        },
                        Text {
                            .text = "or",
                            .font = Font::footnote(),
                            .color = Color::secondary(),
                        },
                        LinkButton {
                            .label = "contact support",
                            .disabled = true,
                            .style = LinkButton::Style {.font = Font::footnote()},
                        }
                    )
                },
                Text {
                    .text = "The disabled support link stays readable but no longer behaves like an active target.",
                    .font = Font::footnote(),
                    .color = Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                    .wrapping = TextWrapping::Wrap,
                }
            )
        }
    );
}

Element makeIconButtonTile(Theme const &theme, std::string title, std::string caption, Element action) {
    return VStack {
        .spacing = theme.space2,
        .alignment = Alignment::Start,
        .children = children(
            HStack {
                .alignment = Alignment::Center,
                .children = children(
                    std::move(action),
                    Spacer {}
                )
            },
            Text {
                .text = std::move(title),
                .font = Font::title3(),
                .color = Color::primary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
            },
            Text {
                .text = std::move(caption),
                .font = Font::footnote(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
                .wrapping = TextWrapping::Wrap,
            }
        )
    }
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius {theme.radiusMedium})
        .flex(1.f, 1.f, 0.f);
}

Element makeIconButtonDemo(Theme const &theme, Signal<std::string> lastEvent) {
    return makeSectionCard(
        theme, "Icon Buttons",
        "Compact icon-only actions fit dense toolbars and utility clusters where a text label would add noise. They should still keep the same hover, focus, and disabled behavior as their text counterparts.",
        Grid {
            .columns = 3,
            .horizontalSpacing = theme.space3,
            .verticalSpacing = theme.space3,
            .children = children(
                makeIconButtonTile(
                    theme, "Play",
                    "A compact start action that works well in media and job-control rows.",
                    IconButton {
                        .icon = IconName::PlayArrow,
                        .style = {
                            .size = theme.largeTitleFont.size,
                        },
                        .onTap =
                            [lastEvent] {
                                lastEvent = "Play pressed from the icon-button section.";
                                std::fprintf(stderr, "[button-demo] Play icon button\n");
                            },
                    }
                ),
                makeIconButtonTile(
                    theme, "Settings",
                    "Utility actions often read more cleanly as icons once the surrounding context is clear.",
                    IconButton {
                        .icon = IconName::Settings,
                        .style = {
                            .size = theme.largeTitleFont.size,
                        },
                        .onTap =
                            [lastEvent] {
                                lastEvent = "Settings pressed from the icon-button section.";
                                std::fprintf(stderr, "[button-demo] Settings icon button\n");
                            },
                    }
                ),
                makeIconButtonTile(
                    theme, "Disabled",
                    "Disabled icon buttons should remain visible and legible without acting interactive.",
                    IconButton {
                        .icon = IconName::Delete,
                        .disabled = true,
                        .style = {
                            .size = theme.largeTitleFont.size,
                        },
                    }
                )
            )
        }
    );
}

} // namespace

struct ButtonDemoRoot {
    Element body() const {
        auto theme = useEnvironment<ThemeKey>();

        auto dirty = useState(true);
        auto reviewPassed = useState(false);
        auto saveCount = useState(3);
        auto publishCount = useState(1);
        auto lastEvent = useState(std::string {
            "Try the sections below. Save is also available from the keyboard with Cmd+S when the draft is dirty."
        });

        useWindowAction(
            "demo.save",
            [dirty, saveCount, lastEvent] {
                if (!*dirty) {
                    return;
                }
                dirty = false;
                saveCount = *saveCount + 1;
                lastEvent = "Draft saved from the keyboard shortcut.";
                std::fprintf(stderr, "[button-demo] Cmd+S save\n");
            },
            [dirty] { return *dirty; }
        );

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = theme().space4,
                    .alignment = Alignment::Start,
                    .children = children(
                        Text {
                            .text = "Button Demo",
                            .font = Font::largeTitle(),
                            .color = Color::primary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                        },
                        Text {
                            .text = "A cleaner button showcase with real layout context: one strong primary action, calm supporting controls, inline links, and a compact toolbar.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .horizontalAlignment = HorizontalAlignment::Leading,
                            .wrapping = TextWrapping::Wrap,
                        },
                        makeHeroDemo(theme(), dirty, reviewPassed, saveCount, lastEvent),
                        makeVariantGallery(theme(), lastEvent),
                        makeIconButtonDemo(theme(), lastEvent),
                        makeToolbarDemo(theme(), dirty, reviewPassed, saveCount, publishCount, lastEvent),
                        makeInlineDemo(theme(), reviewPassed, lastEvent)
                    )
                } //
                    .padding(theme().space5)
            )
        } //
            .fill(FillStyle::solid(Color::windowBackground()));
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);

    auto &w = app.createWindow({
        .size = {800, 800},
        .title = "Lambda - Button demo",
    });

    w.registerAction("demo.save",
                     {
                         .label = "Save draft",
                         .shortcut = shortcuts::Save,
                     });

    w.setView<ButtonDemoRoot>();
    return app.exec();
}
