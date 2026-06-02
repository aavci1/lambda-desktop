#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/ListView.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/VStack.hpp>

namespace lambda {

namespace {

ListRow::Style resolveRowStyle(ListRow::Style const &style, Theme const &theme) {
    return ListRow::Style {
        .paddingH = resolveFloat(style.paddingH, theme.space4),
        .paddingV = resolveFloat(style.paddingV, theme.space3),
    };
}

ListView::Style resolveListStyle(ListView::Style const &style, Theme const &theme) {
    return ListView::Style {
        .dividerInsetH = resolveFloat(style.dividerInsetH, theme.space4),
    };
}

} // namespace

Element ListRow::body() const {
    auto theme = useEnvironment<ThemeKey>();
    ListRow::Style const resolved = resolveRowStyle(style, theme());
    Reactive::Signal<bool> hovered = useHover();
    Reactive::Signal<bool> pressed = usePress();
    bool const isDisabled = disabled;
    Reactive::Bindable<bool> const isSelected = selected;

    Reactive::Bindable<Color> const fill{[isSelected, pressed, hovered, theme] {
        return isSelected.evaluate()
               ? theme().selectedContentBackgroundColor :
               pressed.get() ? theme().rowHoverBackgroundColor :
               hovered.get() ? theme().hoveredControlBackgroundColor :
                               Colors::transparent;
    }};

    auto handleTap = [onTap = onTap, isDisabled]() {
        if (!isDisabled && onTap) {
            onTap();
        }
    };
    auto handleKey = [handleTap, onKeyDown = onKeyDown](KeyCode key, Modifiers modifiers) {
        if (onKeyDown) {
            onKeyDown(key, modifiers);
            return;
        }
        if (key == keys::Return || key == keys::Space) {
            handleTap();
        }
    };

    Element rowContent = content;
    return std::move(rowContent)
        .padding(resolved.paddingV, resolved.paddingH, resolved.paddingV, resolved.paddingH)
        .fill(fill)
        .cursor(isDisabled ? Cursor::Arrow : Cursor::Hand)
        .focusable(!isDisabled)
        .onKeyDown(isDisabled ? std::function<void(KeyCode, Modifiers)>{} : std::function<void(KeyCode, Modifiers)>{handleKey})
        .onTap(isDisabled ? std::function<void()>{} : std::function<void()>{handleTap});
}

Element ListView::body() const {
    auto theme = useEnvironment<ThemeKey>();
    ListView::Style const resolved = resolveListStyle(style, theme());

    std::vector<Element> childrenList;
    childrenList.reserve(showDividers && !rows.empty() ? rows.size() * 2 - 1 : rows.size());

    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (showDividers && i > 0) {
            childrenList.emplace_back(
                Rectangle {}
                    .size(0.f, 1.f)
                    .fill(FillStyle::solid(Color::separator()))
                    .padding(0.f, resolved.dividerInsetH, 0.f, resolved.dividerInsetH)
            );
        }
        childrenList.push_back(rows[i]);
    }

    return ScrollView {
        .axis = ScrollAxis::Vertical,
        .children = children(
            VStack {
                .spacing = 0.f,
                .alignment = Alignment::Stretch,
                .children = std::move(childrenList),
            }
        ),
    };
}

} // namespace lambda
