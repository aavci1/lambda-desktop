#include <Lambda.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Select.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/Toast.hpp>
#include <Lambda/UI/Views/Toggle.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace lambdaui;

namespace {

std::vector<SelectOption> placementOptions() {
  return {
      {.label = "Top leading", .detail = "Stacks from the upper-left corner."},
      {.label = "Top center", .detail = "Stacks across the top edge."},
      {.label = "Top trailing", .detail = "Stacks from the upper-right corner."},
      {.label = "Bottom leading", .detail = "Stacks from the lower-left corner."},
      {.label = "Bottom center", .detail = "Default queue placement."},
      {.label = "Bottom trailing", .detail = "Stacks from the lower-right corner."},
  };
}

ToastPlacement placementFromIndex(int index) {
  switch (index) {
    case 0:
      return ToastPlacement::TopLeading;
    case 1:
      return ToastPlacement::TopCenter;
    case 2:
      return ToastPlacement::TopTrailing;
    case 3:
      return ToastPlacement::BottomLeading;
    case 4:
      return ToastPlacement::BottomCenter;
    case 5:
      return ToastPlacement::BottomTrailing;
    default:
      return ToastPlacement::BottomCenter;
  }
}

std::string placementLabel(ToastPlacement placement) {
  switch (placement) {
    case ToastPlacement::TopLeading:
      return "top leading";
    case ToastPlacement::TopCenter:
      return "top center";
    case ToastPlacement::TopTrailing:
      return "top trailing";
    case ToastPlacement::BottomLeading:
      return "bottom leading";
    case ToastPlacement::BottomCenter:
      return "bottom center";
    case ToastPlacement::BottomTrailing:
      return "bottom trailing";
  }
  return "bottom center";
}

Element sectionCard(Theme const& theme, std::string title, std::string caption, Element content) {
  return Card {
      .child = VStack {
          .spacing = theme.space3,
          .alignment = Alignment::Stretch,
          .children = children(
              Text {
                  .text = std::move(title),
                  .font = Font::title3(),
                  .color = Color::primary(),
              },
              Text {
                  .text = std::move(caption),
                  .font = Font::body(),
                  .color = Color::secondary(),
                  .horizontalAlignment = HorizontalAlignment::Leading,
                  .wrapping = TextWrapping::Wrap,
              },
              std::move(content))
      },
      .style = Card::Style {
          .padding = theme.space4,
          .cornerRadius = theme.radiusXLarge,
      },
  };
}

struct ToastDemoRoot {
  Element body() const {
    auto theme = useEnvironment<ThemeKey>();

    auto placementIndex = useState<int>(4);
    auto persistent = useState(false);
    auto sequence = useState(1);
    auto lastToastId = useState<std::uint64_t>(0);
    auto visibleCount = useState(0);
    auto status = useState(std::string {"Use the controls below to emit toasts."});

    auto toastApi = useToast();
    auto showToast = std::get<0>(toastApi);
    auto dismissToast = std::get<1>(toastApi);
    auto clearToasts = std::get<2>(toastApi);

    std::vector<SelectOption> const placements = placementOptions();
    auto currentPlacement = [placementIndex] {
      return placementFromIndex(placementIndex());
    };
    auto currentAutoDismissMs = [persistent] {
      return persistent() ? 0 : 4500;
    };

    auto nextSequenceLabel = [sequence]() {
      int const current = *sequence;
      sequence = current + 1;
      return std::to_string(current);
    };

    auto emitToneToast = [showToast, status, lastToastId, visibleCount, persistent, currentPlacement,
                          currentAutoDismissMs, nextSequenceLabel](ToastTone tone, std::string title,
                                                                   std::string message) mutable {
      std::string const index = nextSequenceLabel();
      ToastPlacement const placement = currentPlacement();
      std::uint64_t const toastId = showToast(Toast {
          .title = std::move(title) + " #" + index,
          .message = std::move(message),
          .tone = tone,
          .placement = placement,
          .autoDismissMs = currentAutoDismissMs(),
      });
      lastToastId = toastId;
      visibleCount = visibleCount() + 1;
      status = std::string {"Sent a "} + (persistent() ? "persistent" : "timed") + " toast to " +
               placementLabel(placement) + ".";
    };

    Element placementSection = sectionCard(
        theme(), "Placement and Lifetime",
        "Choose the target placement and toggle whether new toasts auto-dismiss or stay visible until dismissed.",
        VStack {
            .spacing = theme().space3,
            .alignment = Alignment::Stretch,
            .children = children(
                Select {
                    .selectedIndex = placementIndex,
                    .options = placements,
                    .helperText = "The queue stacks independently for each placement.",
                },
                HStack {
                    .spacing = theme().space2,
                    .alignment = Alignment::Center,
                    .children = children(
                        Toggle {.value = persistent},
                        Text {
                            .text = "Make new toasts persistent",
                            .font = Font::body(),
                            .color = Color::primary(),
                        },
                        Spacer {},
                        Text {
                            .text = [persistent] {
                              return persistent() ? "Manual dismissal" : "4.5 second timeout";
                            },
                            .font = Font::footnote(),
                            .color = Color::secondary(),
                        })
                })
        });

    Element toneSection = sectionCard(
        theme(), "Tone Variants",
        "These buttons emit one toast each using the selected placement and lifetime policy.",
        VStack {
            .spacing = theme().space2,
            .alignment = Alignment::Stretch,
            .children = children(
                HStack {
                    .spacing = theme().space2,
                    .alignment = Alignment::Center,
                    .children = children(
                        Button {
                            .label = "Neutral",
                            .variant = ButtonVariant::Secondary,
                            .onTap = [emitToneToast] mutable {
                              emitToneToast(
                                  ToastTone::Neutral, "Queued",
                                  "A neutral toast works for low-emphasis system updates.");
                            },
                        }
                            .flex(1.f, 1.f, 0.f),
                        Button {
                            .label = "Accent",
                            .variant = ButtonVariant::Primary,
                            .onTap = [emitToneToast] mutable {
                              emitToneToast(
                                  ToastTone::Accent, "Published",
                                  "Accent tone highlights progress or a primary state change.");
                            },
                        }
                            .flex(1.f, 1.f, 0.f),
                        Button {
                            .label = "Success",
                            .variant = ButtonVariant::Secondary,
                            .onTap = [emitToneToast] mutable {
                              emitToneToast(
                                  ToastTone::Success, "Backup complete",
                                  "Success tone confirms a completed flow without blocking the page.");
                            },
                        }
                            .flex(1.f, 1.f, 0.f))
                },
                HStack {
                    .spacing = theme().space2,
                    .alignment = Alignment::Center,
                    .children = children(
                        Button {
                            .label = "Warning",
                            .variant = ButtonVariant::Secondary,
                            .onTap = [emitToneToast] mutable {
                              emitToneToast(
                                  ToastTone::Warning, "Review needed",
                                  "Warning tone fits recoverable issues that still need attention.");
                            },
                        }
                            .flex(1.f, 1.f, 0.f),
                        Button {
                            .label = "Danger",
                            .variant = ButtonVariant::Destructive,
                            .onTap = [emitToneToast] mutable {
                              emitToneToast(
                                  ToastTone::Danger, "Deployment failed",
                                  "Danger tone is reserved for destructive actions or failed operations.");
                            },
                        }
                            .flex(1.f, 1.f, 0.f),
                        Spacer {})
                })
        });

    Element behaviorSection = sectionCard(
        theme(), "Behavior",
        "Demonstrates stacking, multiple simultaneous placements, inline actions, and dismiss controls.",
        VStack {
            .spacing = theme().space2,
            .alignment = Alignment::Stretch,
            .children = children(
                HStack {
                    .spacing = theme().space2,
                    .alignment = Alignment::Center,
                    .children = children(
                        Button {
                            .label = "Toast with action",
                            .variant = ButtonVariant::Secondary,
                            .onTap = [showToast, status, lastToastId, visibleCount, currentPlacement, currentAutoDismissMs] {
                              std::uint64_t const toastId = showToast(Toast {
                                  .title = "Draft archived",
                                  .message = "Actions can run inline logic and optionally dismiss the toast.",
                                  .tone = ToastTone::Accent,
                                  .placement = currentPlacement(),
                                  .action = ToastAction {
                                      .label = "Undo",
                                      .variant = ButtonVariant::Ghost,
                                      .dismissOnTap = true,
                                      .action = [status] {
                                          status = "Undo tapped from the toast action.";
                                      },
                                  },
                                  .autoDismissMs = currentAutoDismissMs() > 0 ? 7000 : 0,
                              });
                              lastToastId = toastId;
                              visibleCount = visibleCount() + 1;
                              status = "Sent an actionable toast.";
                            },
                        }
                            .flex(1.f, 1.f, 0.f),
                        Button {
                            .label = "Queue three",
                            .variant = ButtonVariant::Secondary,
                            .onTap = [showToast, status, visibleCount, currentPlacement, sequence] {
                              ToastPlacement const placement = currentPlacement();
                              for (ToastTone tone :
                                   {ToastTone::Accent, ToastTone::Success, ToastTone::Warning}) {
                                int const current = *sequence;
                                sequence = current + 1;
                                showToast(Toast {
                                    .title = "Queued item #" + std::to_string(current),
                                    .message = "Independent items should stack cleanly in one placement.",
                                    .tone = tone,
                                    .placement = placement,
                                    .autoDismissMs = 5000,
                                });
                              }
                              visibleCount = visibleCount() + 3;
                              status = "Queued three stacked toasts in the selected placement.";
                            },
                        }
                            .flex(1.f, 1.f, 0.f))
                },
                HStack {
                    .spacing = theme().space2,
                    .alignment = Alignment::Center,
                    .children = children(
                        Button {
                            .label = "Mixed placements",
                            .variant = ButtonVariant::Secondary,
                            .onTap = [showToast, status, visibleCount] {
                              showToast(Toast {
                                  .title = "Sync complete",
                                  .message = "Top-left stack",
                                  .tone = ToastTone::Success,
                                  .placement = ToastPlacement::TopLeading,
                                  .autoDismissMs = 4500,
                              });
                              showToast(Toast {
                                  .title = "Needs review",
                                  .message = "Top-right stack",
                                  .tone = ToastTone::Warning,
                                  .placement = ToastPlacement::TopTrailing,
                                  .autoDismissMs = 4500,
                              });
                              showToast(Toast {
                                  .title = "New comment",
                                  .message = "Bottom-center stack",
                                  .tone = ToastTone::Accent,
                                  .placement = ToastPlacement::BottomCenter,
                                  .autoDismissMs = 4500,
                              });
                              visibleCount = visibleCount() + 3;
                              status = "Sent toasts to three placements at once.";
                            },
                        }
                            .flex(1.f, 1.f, 0.f),
                        Button {
                            .label = "Dismiss last toast",
                            .variant = ButtonVariant::Ghost,
                            .onTap = [dismissToast, lastToastId, visibleCount, status] {
                              if (*lastToastId != 0) {
                                dismissToast(*lastToastId);
                                visibleCount = std::max(0, visibleCount() - 1);
                                status = "Dismissed the most recently tracked toast.";
                              }
                            },
                        }
                            .flex(1.f, 1.f, 0.f),
                        Button {
                            .label = "Clear all",
                            .variant = ButtonVariant::Ghost,
                            .onTap = [clearToasts, visibleCount, status] {
                              clearToasts();
                              visibleCount = 0;
                              status = "Cleared all visible toasts.";
                            },
                        }
                            .flex(1.f, 1.f, 0.f))
                })
        });

    Element content = VStack {
        .spacing = theme().space4,
        .alignment = Alignment::Stretch,
        .children = children(
            Text {
                .text = "Toast",
                .font = Font::largeTitle(),
                .color = Color::primary(),
            },
            Text {
                .text = "A non-modal overlay queue with placements, multiple concurrent stacks, auto-dismiss, persistent notifications, inline actions, and programmatic dismissal.",
                .font = Font::body(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Leading,
                .wrapping = TextWrapping::Wrap,
            },
            Card {
                .child = HStack {
                    .spacing = theme().space3,
                    .alignment = Alignment::Center,
                    .children = children(
                        VStack {
                            .spacing = theme().space1,
                            .alignment = Alignment::Start,
                            .children = children(
                                Text {
                                    .text = [visibleCount] {
                                      return visibleCount() > 0 ? "Visible toasts on screen" : "Overlay idle";
                                    },
                                    .font = Font::headline(),
                                    .color = [visibleCount, theme] {
                                      return visibleCount() > 0 ? theme().accentColor : theme().secondaryLabelColor;
                                    },
                                },
                                Text {
                                    .text = [status] {
                                      return status();
                                    },
                                    .font = Font::footnote(),
                                    .color = Color::secondary(),
                                    .horizontalAlignment = HorizontalAlignment::Leading,
                                    .wrapping = TextWrapping::Wrap,
                                })
                        }
                            .flex(1.f, 1.f, 0.f),
                        Text {
                            .text = [placementIndex] {
                              return placementLabel(placementFromIndex(placementIndex()));
                            },
                            .font = Font::headline(),
                            .color = Color::tertiary(),
                        })
                },
            },
            std::move(placementSection),
            std::move(toneSection),
            std::move(behaviorSection))
    }
        .padding(theme().space6)
        .width(880.f);

    return ScrollView {
        .axis = ScrollAxis::Vertical,
        .children = children(std::move(content)),
    };
  }
};

} // namespace

int main(int argc, char* argv[]) {
  Application app(argc, argv);
  auto& window = app.createWindow<Window>({
      .size = {980, 820},
      .title = "Lambda — Toast demo",
      .resizable = true,
  });
  window.setView<ToastDemoRoot>();
  return app.exec();
}
