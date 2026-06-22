// Showcases Lambda Icon (Material Symbols Rounded): grid of all curated icons,
// size scale, semantic colours.
#include <Lambda.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <string_view>
#include <string>
#include <vector>

using namespace lambda;

namespace {

struct NamedIcon {
    IconName name;
    std::string_view label;
};

// Same order as IconName.hpp (grouped for the demo UI).
constexpr NamedIcon kIcons[] = {
    {IconName::ArrowBack, "ArrowBack"},
    {IconName::ArrowForward, "ArrowForward"},
    {IconName::ArrowUpward, "ArrowUpward"},
    {IconName::ArrowDownward, "ArrowDownward"},

    {IconName::ChevronLeft, "ChevronLeft"},
    {IconName::ChevronRight, "ChevronRight"},
    {IconName::ExpandLess, "ExpandLess"},
    {IconName::ExpandMore, "ExpandMore"},

    {IconName::Menu, "Menu"},
    {IconName::MoreHoriz, "MoreHoriz"},
    {IconName::MoreVert, "MoreVert"},
    {IconName::Add, "Add"},

    {IconName::Undo, "Undo"},
    {IconName::Redo, "Redo"},
    {IconName::Close, "Close"},
    {IconName::Check, "Check"},

    {IconName::ContentCopy, "ContentCopy"},
    {IconName::ContentCut, "ContentCut"},
    {IconName::ContentPaste, "ContentPaste"},
    {IconName::Delete, "Delete"},

    {IconName::FolderOpen, "FolderOpen"},
    {IconName::Edit, "Edit"},
    {IconName::Save, "Save"},
    {IconName::Search, "Search"},

    {IconName::CheckCircle, "CheckCircle"},
    {IconName::Error, "Error"},
    {IconName::Info, "Info"},
    {IconName::Warning, "Warning"},

    {IconName::DarkMode, "DarkMode"},
    {IconName::LightMode, "LightMode"},
    {IconName::Visibility, "Visibility"},
    {IconName::VisibilityOff, "VisibilityOff"},

    {IconName::FormatBold, "FormatBold"},
    {IconName::FormatItalic, "FormatItalic"},
    {IconName::FormatListBulleted, "FormatListBulleted"},
    {IconName::FormatListNumbered, "FormatListNumbered"},
};

struct IconCell {
    IconName name;
    std::string label;

    bool operator==(IconCell const &) const = default;

    auto body() const {
        auto theme = useEnvironment<ThemeKey>();

        return VStack {
            .spacing = theme().space2,
            .alignment = Alignment::Center,
            .children = children(
                Icon {
                    .name = name,
                    .size = 32.f,
                    .weight = 600.f,
                    .color = Color::secondary()
                },
                Spacer {},
                Text {
                    .text = label,
                    .font = Font::footnote(),
                    .color = Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Center,
                    .wrapping = TextWrapping::Wrap,
                }
            ),
        }
            .padding(theme().space2);
    }
};

struct IconDemoRoot {
    auto body() const {
        auto theme = useEnvironment<ThemeKey>();

        std::vector<Element> gridCells;
        gridCells.reserve(sizeof(kIcons) / sizeof(kIcons[0]));
        for (NamedIcon const &item : kIcons) {
            gridCells.push_back(
                IconCell {
                    .name = item.name,
                    .label = std::string(item.label),
                }
            );
        }

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = theme().space4,
                    .children = children(
                        Text {
                            .text = "Icon demo",
                            .font = Font::largeTitle(),
                            .color = Color::primary()
                        },
                        Text {
                            .text = "Material Symbols Rounded — curated "
                                    "IconName setheme. "
                                    "Resize the window; scroll for the "
                                    "full grid.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .wrapping = TextWrapping::Wrap,
                        },

                        Grid {
                            .columns = 4,
                            .horizontalSpacing = theme().space2,
                            .verticalSpacing = theme().space4,
                            .horizontalAlignment = Alignment::Center,
                            .verticalAlignment = Alignment::Center,
                            .children = std::move(gridCells),
                        }
                            .padding(theme().space4)
                            .fill(FillStyle::solid(Color::elevatedBackground()))
                            .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                            .cornerRadius(CornerRadius {theme().radiusLarge})
                    ),
                }
                    .padding(theme().space5)
            ),
        };
    }
};

} // namespace

int main(int argc, char *argv[]) {
    Application app(argc, argv);
    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda — Icon demo",
        .resizable = true,
    });
    w.setView<IconDemoRoot>();
    return app.exec();
}
