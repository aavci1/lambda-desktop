#pragma once

/// \file Lambda/UI/Views/TableView.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Core/Color.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>

#include <any>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace lambda {

struct TableColumn {
    struct Sort {
        /// Initial direction when the user first sorts this column.
        bool initialAscending = true;
        /// Comparator for sort payloads stored in `SortValue`.
        std::function<bool(std::any const &, std::any const &)> less;

        bool operator==(Sort const& other) const {
            return initialAscending == other.initialAscending &&
                   static_cast<bool>(less) == static_cast<bool>(other.less);
        }

        template<typename T, typename Compare = std::less<>>
        static Sort by(Compare compare = Compare {}, bool initialAscending = true) {
            return Sort {
                .initialAscending = initialAscending,
                .less = [compare = std::move(compare)](std::any const &lhs, std::any const &rhs) {
                    return std::invoke(compare, std::any_cast<T const &>(lhs), std::any_cast<T const &>(rhs));
                },
            };
        }

        template<typename T>
        static Sort ascending() {
            return by<T>(std::less<> {}, true);
        }

        template<typename T>
        static Sort descending() {
            return by<T>(std::less<> {}, false);
        }
    };

    /// Preferred fixed width for the column.
    float width = kFloatFromTheme;
    /// Extra horizontal growth factor after fixed widths are satisfied.
    float flexGrow = 0.f;
    /// Optional sort behavior for this column.
    std::optional<Sort> sort {};

    bool operator==(TableColumn const& other) const = default;
};

struct TableCell : ViewModifiers<TableCell> {
    struct Style {
        float width = kFloatFromTheme;
        HorizontalAlignment alignment = HorizontalAlignment::Leading;

        bool operator==(Style const& other) const = default;
    };

    /// Cell body content.
    Element content;
    /// Per-cell width / alignment override.
    Style style {};

    Element body() const;
};

struct TableRow : ViewModifiers<TableRow> {
    struct Style {
        float paddingH = kFloatFromTheme;
        float paddingV = kFloatFromTheme;
        float spacing = kFloatFromTheme;
        Color backgroundColor = Color::theme();
        Color hoverBackgroundColor = Color::theme();
        Color selectedBackgroundColor = Color::theme();

        bool operator==(Style const& other) const = default;
    };

    /// Cell elements in column order.
    std::vector<Element> cells;
    /// Optional expandable detail content rendered under the main row.
    std::optional<Element> detail {};
    /// Selected state for styling.
    bool selected = false;
    /// Prevents interaction when true.
    bool disabled = false;
    /// Optional row token overrides.
    Style style {};
    /// Called when the row is activated.
    std::function<void()> onTap;

    Element body() const;
};

struct TableView : ViewModifiers<TableView> {
    struct SortValue {
        /// Column index this sort value belongs to.
        std::size_t column = 0;
        /// Opaque sort payload consumed by the column comparator.
        std::any value {};

        SortValue() = default;
        SortValue(SortValue const &) = default;
        SortValue(SortValue &&) noexcept = default;
        SortValue &operator=(SortValue const &) = default;
        SortValue &operator=(SortValue &&) noexcept = default;

        template<typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, SortValue>>>
        SortValue(std::size_t columnIndex, T &&sortValue)
            : column(columnIndex)
            , value(std::forward<T>(sortValue)) {}

        bool operator==(SortValue const& other) const {
            // std::any is not structurally comparable. Column identity and value presence are enough
            // for retained subtree safety; actual sorting is recomputed inside TableView::body().
            return column == other.column && value.has_value() == other.value.has_value();
        }
    };

    struct Item {
        /// Row element to render.
        Element row;
        /// Per-column values used when sorting.
        std::vector<SortValue> sortValues;

    };

    struct Style {
        /// Horizontal inset applied to row dividers.
        float dividerInsetH = kFloatFromTheme;
        /// Table background color.
        Color backgroundColor = Color::theme();
        /// Divider color between rows.
        Color dividerColor = Color::theme();

        bool operator==(Style const& other) const = default;
    };

    /// Optional header row rendered above the body.
    std::optional<Element> header {};
    /// Structured item API with built-in sorting metadata.
    std::vector<Item> items;
    /// Simpler row-only API when sorting metadata is not needed.
    std::vector<Element> rows;
    /// Column definitions used for width and sorting behavior.
    std::vector<TableColumn> columns;
    /// Draws row separators when true.
    bool showDividers = true;
    /// Wraps the body rows in a `ScrollView` when true.
    bool scrollBody = true;
    /// Optional token overrides.
    Style style {};

    Element body() const;
};

} // namespace lambda
