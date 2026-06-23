#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Select.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <cstdio>
#include <string>
#include <vector>

using namespace lambdaui;

namespace {

Element makeSectionCard(Theme const &theme, std::string title, std::string caption, Element content) {
  return Card{
      .child = VStack{
          .spacing = theme.space3,
          .children = children(
              Text{
                  .text = std::move(title),
                  .font = Font::title2(),
                  .color = Color::primary(),
                  .horizontalAlignment = HorizontalAlignment::Leading,
              },
              Text{
                  .text = std::move(caption),
                  .font = Font::footnote(),
                  .color = Color::secondary(),
                  .horizontalAlignment = HorizontalAlignment::Leading,
                  .wrapping = TextWrapping::Wrap,
              },
              std::move(content))
      },
      .style = Card::Style{
          .padding = theme.space4,
          .cornerRadius = theme.radiusLarge,
      },
  };
}

Element metricTile(Theme const &theme, Bindable<std::string> value, std::string label, Color accent) {
  return VStack{
      .spacing = theme.space1,
      .alignment = Alignment::Start,
      .children = children(
          Text{
              .text = std::move(value),
              .font = Font::title2(),
              .color = accent,
              .horizontalAlignment = HorizontalAlignment::Leading,
          },
          Text{
              .text = std::move(label),
              .font = Font::footnote(),
              .color = Color::secondary(),
              .horizontalAlignment = HorizontalAlignment::Leading,
          })
  }
      .padding(theme.space3)
      .fill(FillStyle::solid(Color::windowBackground()))
      .cornerRadius(CornerRadius{theme.radiusMedium})
      .flex(1.f, 1.f, 0.f);
}

Element controlBlock(Theme const &theme, std::string label, std::string caption, Element control) {
  return VStack{
      .spacing = theme.space2,
      .alignment = Alignment::Start,
      .children = children(
          Text{
              .text = std::move(label),
              .font = Font::headline(),
              .color = Color::primary(),
              .horizontalAlignment = HorizontalAlignment::Leading,
          },
          Text{
              .text = std::move(caption),
              .font = Font::footnote(),
              .color = Color::secondary(),
              .horizontalAlignment = HorizontalAlignment::Leading,
              .wrapping = TextWrapping::Wrap,
          },
          std::move(control))
  }
      .padding(theme.space3)
      .fill(FillStyle::solid(Color::windowBackground()))
      .cornerRadius(CornerRadius{theme.radiusMedium});
}

std::vector<SelectOption> environmentOptions() {
  return {
      {.label = "Production", .detail = "Live traffic and customer-visible changes."},
      {.label = "Staging", .detail = "Release candidate validation before rollout."},
      {.label = "QA sandbox", .detail = "Safe playground for exploratory testing."},
      {.label = "Local preview", .detail = "Fastest loop for visual polish and debugging."},
  };
}

std::vector<SelectOption> reviewerOptions() {
  return {
      {.label = "Mina Chen", .detail = "Design systems"},
      {.label = "Sam Ortega", .detail = "Release engineering"},
      {.label = "Priya Kapoor", .detail = "On leave until Monday", .disabled = true},
      {.label = "Jonas Meyer", .detail = "Product and editorial QA"},
  };
}

std::vector<SelectOption> sprintOptions() {
  return {
      {.label = "Sprint 14", .detail = "Navigation cleanup"},
      {.label = "Sprint 15", .detail = "Checkout resilience"},
      {.label = "Sprint 16", .detail = "Notification revamp"},
      {.label = "Sprint 17", .detail = "Search relevance tuning"},
      {.label = "Sprint 18", .detail = "Billing migration"},
      {.label = "Sprint 19", .detail = "Analytics instrumentation"},
      {.label = "Sprint 20", .detail = "Localization pass"},
      {.label = "Sprint 21", .detail = "Accessibility fixes"},
      {.label = "Sprint 22", .detail = "Marketing handoff"},
      {.label = "Sprint 23", .detail = "Dashboard performance"},
      {.label = "Sprint 24", .detail = "Release hardening"},
      {.label = "Sprint 25", .detail = "Partner beta"},
  };
}

std::vector<SelectOption> densityOptions() {
  return {
      {.label = "Comfortable", .detail = "Roomier panels for review sessions."},
      {.label = "Balanced", .detail = "Default density for everyday work."},
      {.label = "Compact", .detail = "Fits more controls in dense admin surfaces."},
  };
}

std::string selectedLabel(std::vector<SelectOption> const &options, int index, std::string fallback) {
  if (index >= 0 && static_cast<std::size_t>(index) < options.size()) {
    return options[static_cast<std::size_t>(index)].label;
  }
  return fallback;
}

} // namespace

struct SelectDemoRoot {
  Element body() const {
    auto theme = useEnvironment<ThemeKey>();

    std::vector<SelectOption> const envOptions = environmentOptions();
    std::vector<SelectOption> const peopleOptions = reviewerOptions();
    std::vector<SelectOption> const backlogOptions = sprintOptions();
    std::vector<SelectOption> const densityChoices = densityOptions();

    auto environment = useState<int>(1);
    auto reviewer = useState<int>(-1);
    auto sprint = useState<int>(7);
    auto density = useState<int>(1);
    auto lastEvent = useState<std::string>("Open a menu and make a selection to watch the summary update.");

    Bindable<std::string> configuredCount{[environment, reviewer, sprint] {
      int const count = (environment() >= 0 ? 1 : 0) +
                        (reviewer() >= 0 ? 1 : 0) +
                        (sprint() >= 0 ? 1 : 0);
      return std::to_string(count);
    }};

    return ScrollView{
        .axis = ScrollAxis::Vertical,
        .children = children(
            VStack{
                .spacing = theme().space4,
                .children = children(
                    Text{
                        .text = "Select Demo",
                        .font = Font::largeTitle(),
                        .color = Color::primary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                    },
                    Text{
                        .text = "A popover-backed dropdown with placeholders, detailed options, disabled rows, keyboard navigation on the trigger, configurable placement, and scrollable menus for longer datasets.",
                        .font = Font::body(),
                        .color = Color::secondary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                        .wrapping = TextWrapping::Wrap,
                    },
                    HStack{
                        .spacing = theme().space3,
                        .alignment = Alignment::Stretch,
                        .children = children(
                            metricTile(theme(), configuredCount, "Configured fields", Color::accent()),
                            metricTile(theme(), [sprint, backlogOptions] {
                              return selectedLabel(backlogOptions, sprint(), "None");
                            }, "Active sprint", Color::success()),
                            metricTile(theme(), [density, densityChoices] {
                              return selectedLabel(densityChoices, density(), "Balanced");
                            }, "Density preset", Color::warning()))
                    },
                    makeSectionCard(
                        theme(), "Release Setup",
                        "The core control works well as a field in a real form, with placeholder text, helper copy, and richer option details.",
                        VStack{
                            .spacing = theme().space3,
                            .children = children(
                                controlBlock(
                                    theme(), "Environment",
                                    "Arrow keys cycle between enabled options. Press Space or Return to open the full menu.",
                                    Select{
                                        .selectedIndex = environment,
                                        .options = envOptions,
                                        .helperText = "Choosing a target here updates deployment defaults for the rest of the panel.",
                                        .onChange =
                                            [lastEvent, envOptions](int index) {
                                              lastEvent = "Environment set to " + envOptions[static_cast<std::size_t>(index)].label + ".";
                                              std::fprintf(stderr, "[select-demo] Environment -> %s\n",
                                                           envOptions[static_cast<std::size_t>(index)].label.c_str());
                                            },
                                    }),
                                controlBlock(
                                    theme(), "Reviewer",
                                    "Disabled rows remain visible in the menu so availability is still explained instead of silently disappearing.",
                                    Select{
                                        .selectedIndex = reviewer,
                                        .options = peopleOptions,
                                        .placeholder = "Assign a reviewer",
                                        .helperText = "Priya is intentionally disabled to demonstrate unavailable options in context.",
                                        .onChange =
                                            [lastEvent, peopleOptions](int index) {
                                              lastEvent = "Reviewer assigned to " + peopleOptions[static_cast<std::size_t>(index)].label + ".";
                                              std::fprintf(stderr, "[select-demo] Reviewer -> %s\n",
                                                           peopleOptions[static_cast<std::size_t>(index)].label.c_str());
                                            },
                                    }))
                        }),
                    makeSectionCard(
                        theme(), "Long Menu",
                        "A longer backlog list is clipped into a scrollable menu instead of expanding past the viewport.",
                        controlBlock(
                            theme(), "Sprint backlog",
                            "This list is intentionally long so the menu has to scroll. Home and End jump to the first and last enabled options.",
                            Select{
                                .selectedIndex = sprint,
                                .options = backlogOptions,
                                .helperText = "Try opening this one near the middle of the page and scroll through the menu with the wheel or by dragging.",
                                .style = Select::Style{
                                    .menuMaxHeight = 220.f,
                                },
                                .onChange =
                                    [lastEvent, backlogOptions](int index) {
                                      lastEvent = "Sprint changed to " + backlogOptions[static_cast<std::size_t>(index)].label + ".";
                                      std::fprintf(stderr, "[select-demo] Sprint -> %s\n",
                                                   backlogOptions[static_cast<std::size_t>(index)].label.c_str());
                                    },
                            })),
                    makeSectionCard(
                        theme(), "Style Variations",
                        "The same component can adapt to denser utility surfaces or upward-opening menus without changing its interaction model.",
                        VStack{
                            .spacing = theme().space3,
                            .children = children(
                                controlBlock(
                                    theme(), "Density preset",
                                    "This variation uses a success accent and a tighter menu height to feel more compact.",
                                    Select{
                                        .selectedIndex = density,
                                        .options = densityChoices,
                                        .helperText = "A small token shift is enough to fit the control into a different surface personality.",
                                        .style = Select::Style{
                                            .menuMaxHeight = 180.f,
                                            .accentColor = Color::success(),
                                            .fieldHoverColor = Color::successBackground(),
                                            .rowHoverColor = Color{theme().successBackgroundColor.r, theme().successBackgroundColor.g, theme().successBackgroundColor.b, 0.4f},
                                        },
                                        .onChange =
                                            [lastEvent, densityChoices](int index) {
                                              lastEvent = "Density updated to " + densityChoices[static_cast<std::size_t>(index)].label + ".";
                                              std::fprintf(stderr, "[select-demo] Density -> %s\n",
                                                           densityChoices[static_cast<std::size_t>(index)].label.c_str());
                                            },
                                    }),
                                controlBlock(
                                    theme(), "Placement override",
                                    "This select prefers opening upward, which is useful for controls anchored near the bottom edge of a window or sheet.",
                                    Select{
                                        .selectedIndex = environment,
                                        .options = envOptions,
                                        .helperText = "Scroll this demo near the bottom of the window and open the menu to see the upward bias.",
                                        .placement = PopoverPlacement::Above,
                                        .onChange =
                                            [lastEvent, envOptions](int index) {
                                              lastEvent = "Placement demo now points at " + envOptions[static_cast<std::size_t>(index)].label + ".";
                                            },
                                    }),
                                controlBlock(
                                    theme(), "Disabled state",
                                    "Disabled selects keep their current value visible without reacting to hover, keyboard, or pointer input.",
                                    Select{
                                        .selectedIndex = density,
                                        .options = densityChoices,
                                        .helperText = "Useful for inherited settings or gated features that still need to communicate their current value.",
                                        .disabled = true,
                                    }))
                        }),
                    Text{
                        .text = [lastEvent] {
                          return lastEvent();
                        },
                        .font = Font::footnote(),
                        .color = Color::tertiary(),
                        .horizontalAlignment = HorizontalAlignment::Leading,
                        .wrapping = TextWrapping::Wrap,
                    })
            }
                .padding(theme().space5))
    }
        .fill(FillStyle::solid(Color::windowBackground()));
  }
};

int main(int argc, char *argv[]) {
  Application app(argc, argv);
  auto &w = app.createWindow<Window>({
      .size = {800, 800},
      .title = "Lambda - Select demo",
      .resizable = true,
  });
  w.setView<SelectDemoRoot>();
  return app.exec();
}
