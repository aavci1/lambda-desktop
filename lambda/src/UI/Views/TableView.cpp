#include <algorithm>
#include <numeric>
#include <string>

#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Views/TableView.hpp>
#include <Lambda/UI/Views/Badge.hpp>
#include <Lambda/UI/Views/For.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

namespace lambdaui {

struct TableColumnLayout {
    float width = 0.f;
    float flexGrow = 0.f;

    bool operator==(TableColumnLayout const &) const = default;
};

struct TableLayoutContext {
    std::vector<TableColumnLayout> columns;

    bool operator==(TableLayoutContext const &) const = default;
};

struct TableColumnIndex {
    std::size_t value = 0;

    bool operator==(TableColumnIndex const &) const = default;
};

struct TableLayoutContextKey {};
template<>
struct EnvironmentKey<TableLayoutContextKey> {
    using Value = TableLayoutContext;
    static Value defaultValue() { return {}; }
    static ::lambdaui::detail::EnvironmentSlot const& slot() {
        static ::lambdaui::detail::EnvironmentSlot s{
            ::lambdaui::detail::allocateEnvironmentSlot(typeid(TableLayoutContextKey))};
        return s;
    }
};

struct TableColumnIndexKey {};
template<>
struct EnvironmentKey<TableColumnIndexKey> {
    using Value = TableColumnIndex;
    static Value defaultValue() { return {}; }
    static ::lambdaui::detail::EnvironmentSlot const& slot() {
        static ::lambdaui::detail::EnvironmentSlot s{
            ::lambdaui::detail::allocateEnvironmentSlot(typeid(TableColumnIndexKey))};
        return s;
    }
};

struct RenderedTableRow {
    std::string key;
    Element row;

    bool operator==(RenderedTableRow const &other) const {
        return key == other.key;
    }
};

TableRow::Style resolveRowStyle(TableRow::Style const &style, Theme const &theme) {
    return TableRow::Style {
        .paddingH = resolveFloat(style.paddingH, theme.space4),
        .paddingV = resolveFloat(style.paddingV, theme.space2),
        .spacing = resolveFloat(style.spacing, theme.space2),
        .backgroundColor = resolveColor(style.backgroundColor, theme.elevatedBackgroundColor, theme),
        .hoverBackgroundColor = resolveColor(style.hoverBackgroundColor, theme.rowHoverBackgroundColor, theme),
        .selectedBackgroundColor = resolveColor(style.selectedBackgroundColor, theme.selectedContentBackgroundColor, theme),
    };
}

TableView::Style resolveTableStyle(TableView::Style const &style, Theme const &theme) {
    return TableView::Style {
        .dividerInsetH = resolveFloat(style.dividerInsetH, theme.space4),
        .backgroundColor = resolveColor(style.backgroundColor, theme.windowBackgroundColor, theme),
        .dividerColor = resolveColor(style.dividerColor, theme.separatorColor, theme),
    };
}

Alignment resolveHorizontalAlignment(HorizontalAlignment alignment) {
    switch (alignment) {
        case HorizontalAlignment::Leading:
            return Alignment::Start;
        case HorizontalAlignment::Center:
            return Alignment::Center;
        case HorizontalAlignment::Trailing:
            return Alignment::End;
    }
    return Alignment::Start;
}

Element divider(Color color, float insetH) {
    return Rectangle {}
        .size(0.f, 1.f)
        .fill(FillStyle::solid(color))
        .padding(0.f, insetH, 0.f, insetH);
}

bool isSortable(TableColumn const &column) {
    return column.sort && static_cast<bool>(column.sort->less);
}

TableColumn::Sort const *activeSort(std::vector<TableColumn> const &columns, int activeColumn) {
    if (activeColumn < 0 || static_cast<std::size_t>(activeColumn) >= columns.size()) {
        return nullptr;
    }
    TableColumn const &column = columns[static_cast<std::size_t>(activeColumn)];
    if (!isSortable(column)) {
        return nullptr;
    }
    return &*column.sort;
}

std::any const *findSortValue(TableView::Item const &item, std::size_t columnIndex) {
    for (TableView::SortValue const &entry : item.sortValues) {
        if (entry.column == columnIndex) {
            return &entry.value;
        }
    }
    return nullptr;
}

bool shouldPlaceBefore(TableView::Item const &lhs, TableView::Item const &rhs, std::size_t columnIndex,
                       TableColumn::Sort const &sort, bool ascending) {
    std::any const *lhsValue = findSortValue(lhs, columnIndex);
    std::any const *rhsValue = findSortValue(rhs, columnIndex);

    bool const lhsPresent = lhsValue && lhsValue->has_value();
    bool const rhsPresent = rhsValue && rhsValue->has_value();
    if (lhsPresent != rhsPresent) {
        return lhsPresent;
    }
    if (!lhsPresent) {
        return false;
    }

    bool const lhsBeforeRhs = sort.less(*lhsValue, *rhsValue);
    bool const rhsBeforeLhs = sort.less(*rhsValue, *lhsValue);
    if (lhsBeforeRhs == rhsBeforeLhs) {
        return false;
    }
    return ascending ? lhsBeforeRhs : rhsBeforeLhs;
}

Element sortIndicator(std::size_t columnIndex,
                      Theme const &theme,
                      Signal<int> sortColumn,
                      Signal<bool> sortAscending) {
    int const column = static_cast<int>(columnIndex);
    return Icon {
        .name = [sortColumn, sortAscending, column] {
            return sortColumn.get() == column
                       ? (sortAscending.get() ? IconName::ArrowUpward : IconName::ArrowDownward)
                       : IconName::Sort;
        },
        .size = 16.f,
        .color = [sortColumn,
                  column,
                  accent = theme.accentColor,
                  tertiary = theme.tertiaryLabelColor] {
            return sortColumn.get() == column ? accent : tertiary;
        },
    };
}

Element makeSortableHeaderCell(Element cell, std::size_t columnIndex, TableColumn::Sort sort,
                               Theme const &theme, Signal<int> sortColumn,
                               Signal<bool> sortAscending) {
    auto activateSort = [columnIndex, sort = std::move(sort), sortColumn, sortAscending] {
        int const requested = static_cast<int>(columnIndex);
        if (*sortColumn == requested) {
            sortAscending = !*sortAscending;
            return;
        }
        sortColumn = requested;
        sortAscending = sort.initialAscending;
    };
    auto handleKey = [activateSort](KeyCode key, Modifiers) {
        if (key == keys::Return || key == keys::Space) {
            activateSort();
        }
    };

    if (cell.is<TableCell>()) {
        TableCell headerCell = cell.as<TableCell>();
        headerCell.content = HStack {
            .spacing = theme.space1,
            .alignment = Alignment::Center,
            .children = children(
                std::move(headerCell.content),
                sortIndicator(columnIndex, theme, sortColumn, sortAscending)
            ),
        };
        cell = Element {std::move(headerCell)};
    }

    return std::move(cell)
        .cursor(Cursor::Hand)
        .focusable(true)
        .onKeyDown(handleKey)
        .onTap(activateSort);
}

Element decorateHeader(Element header, std::vector<TableColumn> const &columns, Theme const &theme,
                       Signal<int> sortColumn, Signal<bool> sortAscending) {
    if (!header.is<TableRow>()) {
        return header;
    }

    TableRow row = header.as<TableRow>();
    for (std::size_t i = 0; i < row.cells.size() && i < columns.size(); ++i) {
        if (!isSortable(columns[i])) {
            continue;
        }
        row.cells[i] = makeSortableHeaderCell(
            std::move(row.cells[i]),
            i,
            *columns[i].sort,
            theme,
            sortColumn,
            sortAscending
        );
    }
    return Element {std::move(row)};
}

std::string itemRowKey(TableView::Item const &item, std::size_t fallbackIndex) {
    if (item.row.explicitKey()) {
        return "item:" + *item.row.explicitKey();
    }
    return "item:" + std::to_string(fallbackIndex);
}

std::vector<std::size_t> sortedItemOrder(std::vector<TableView::Item> const &items,
                                         std::vector<TableColumn> const &columns,
                                         int activeColumn,
                                         bool ascending) {
    std::vector<std::size_t> order(items.size());
    std::iota(order.begin(), order.end(), 0u);
    if (TableColumn::Sort const *sort = activeSort(columns, activeColumn)) {
        std::stable_sort(order.begin(), order.end(), [&](std::size_t lhsIndex, std::size_t rhsIndex) {
            return shouldPlaceBefore(items[lhsIndex], items[rhsIndex], static_cast<std::size_t>(activeColumn), *sort,
                                     ascending);
        });
    }
    return order;
}

std::vector<RenderedTableRow> buildRenderedRows(std::vector<TableView::Item> const &items,
                                                std::vector<Element> const &rows,
                                                std::vector<TableColumn> const &columns,
                                                int activeColumn,
                                                bool ascending,
                                                bool showDividers,
                                                TableView::Style const &resolved) {
    std::vector<std::size_t> const itemOrder = sortedItemOrder(items, columns, activeColumn, ascending);
    std::size_t const totalRows = itemOrder.size() + rows.size();
    std::vector<RenderedTableRow> rendered;
    rendered.reserve(showDividers && totalRows > 0 ? totalRows * 2u - 1u : totalRows);
    std::size_t renderedRowCount = 0;

    auto pushDivider = [&] {
        if (showDividers && renderedRowCount > 0) {
            rendered.push_back(RenderedTableRow {
                .key = "divider:" + std::to_string(renderedRowCount),
                .row = divider(resolved.dividerColor, resolved.dividerInsetH),
            });
        }
    };

    for (std::size_t index : itemOrder) {
        pushDivider();
        rendered.push_back(RenderedTableRow {
            .key = itemRowKey(items[index], index),
            .row = items[index].row,
        });
        ++renderedRowCount;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        pushDivider();
        rendered.push_back(RenderedTableRow {
            .key = "row:" + std::to_string(i),
            .row = rows[i],
        });
        ++renderedRowCount;
    }
    return rendered;
}

Element TableCell::body() const {
    auto table = useEnvironment<TableLayoutContextKey>();
    auto index = useEnvironment<TableColumnIndexKey>();

    float resolvedWidth = style.width > 0.f ? style.width : 0.f;
    bool usesTableFlex = false;
    if (index().value < table().columns.size()) {
        TableColumnLayout const &column = table().columns[index().value];
        if (column.width > 0.f) {
            resolvedWidth = column.width;
        } else if (column.flexGrow > 0.f) {
            usesTableFlex = true;
        }
    }

    Element cellContent = content;
    if (resolvedWidth <= 0.f && !usesTableFlex && style.alignment == HorizontalAlignment::Leading) {
        return cellContent;
    }

    bool const stretchesContent = cellContent.is<Text>() || cellContent.is<Badge>();
    if (cellContent.is<Text>()) {
        Text text = cellContent.as<Text>();
        text.horizontalAlignment = style.alignment;
        cellContent = Element {std::move(text)};
    }

    Element aligned = ZStack {
        .horizontalAlignment = stretchesContent ? Alignment::Stretch : resolveHorizontalAlignment(style.alignment),
        .verticalAlignment = Alignment::Center,
        .children = children(std::move(cellContent)),
    };
    if (resolvedWidth > 0.f) {
        return std::move(aligned).width(resolvedWidth);
    }
    return aligned;
}

Element TableRow::body() const {
    auto theme = useEnvironment<ThemeKey>();
    TableRow::Style const resolved = resolveRowStyle(style, theme());
    auto table = useEnvironment<TableLayoutContextKey>();
    Reactive::Signal<bool> hovered = useHover();
    Reactive::Signal<bool> pressed = usePress();
    bool const interactive = !disabled && static_cast<bool>(onTap);

    Reactive::Bindable<Color> const fill{[selected = selected, pressed, hovered,
                                          interactive, resolved] {
        return selected                     ? resolved.selectedBackgroundColor :
               pressed.get() && interactive ? resolved.hoverBackgroundColor :
               hovered.get()                ? resolved.hoverBackgroundColor :
                                              resolved.backgroundColor;
    }};

    auto handleTap = [onTap = onTap, disabled = disabled] {
        if (!disabled && onTap) {
            onTap();
        }
    };
    auto handleKey = [handleTap](KeyCode key, Modifiers) {
        if (key == keys::Return || key == keys::Space) {
            handleTap();
        }
    };

    std::vector<Element> rowCells;
    rowCells.reserve(cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        Element cell = cells[i];
        cell = std::move(cell).environment<TableColumnIndexKey>(TableColumnIndex {.value = i});
        if (i < table().columns.size()) {
            TableColumnLayout const &column = table().columns[i];
            if (column.width <= 0.f && column.flexGrow > 0.f) {
                cell = std::move(cell).flex(column.flexGrow, 1.f, 0.f);
            }
        }
        rowCells.push_back(std::move(cell));
    }

    std::vector<Element> childrenList;
    childrenList.reserve(detail ? 2u : 1u);
    childrenList.push_back(
        HStack {
            .spacing = resolved.spacing,
            .alignment = Alignment::Center,
            .children = std::move(rowCells),
        }
            .padding(resolved.paddingV, resolved.paddingH, resolved.paddingV, resolved.paddingH)
            .fill(fill)
    );
    if (detail) {
        childrenList.push_back(*detail);
    }

    return VStack {
        .spacing = 0.f,
        .alignment = Alignment::Stretch,
        .children = std::move(childrenList),
    }
        .cursor(interactive ? Cursor::Hand : Cursor::Arrow)
        .focusable(interactive)
        .onKeyDown(interactive ? std::function<void(KeyCode, Modifiers)> {handleKey}
                               : std::function<void(KeyCode, Modifiers)> {})
        .onTap(interactive ? std::function<void()> {handleTap} : std::function<void()> {});
}

Element TableView::body() const {
    auto theme = useEnvironment<ThemeKey>();
    TableView::Style const resolved = resolveTableStyle(style, theme());
    Signal<int> const sortColumn = useState<int>(-1);
    Signal<bool> const sortAscending = useState<bool>(true);
    TableLayoutContext layout {};
    layout.columns.reserve(columns.size());
    for (TableColumn const &column : columns) {
        layout.columns.push_back(TableColumnLayout {
            .width = column.width > 0.f ? column.width : 0.f,
            .flexGrow = column.width > 0.f ? 0.f : std::max(0.f, column.flexGrow),
        });
    }

    int const activeColumn = activeSort(columns, sortColumn.get()) ? sortColumn.get() : -1;
    Signal<std::vector<RenderedTableRow>> const renderedRows = useState<std::vector<RenderedTableRow>>(
        buildRenderedRows(items, rows, columns, activeColumn, sortAscending.get(), showDividers, resolved)
    );
    useEffect([renderedRows,
               items = items,
               rows = rows,
               columns = columns,
               showDividers = showDividers,
               resolved,
               sortColumn,
               sortAscending] {
        int const column = activeSort(columns, sortColumn.get()) ? sortColumn.get() : -1;
        renderedRows = buildRenderedRows(items, rows, columns, column, sortAscending.get(), showDividers, resolved);
    });

    Element bodyContent = Element {For<RenderedTableRow>(
        renderedRows,
        [](RenderedTableRow const &row) {
            return row.key;
        },
        [layout](RenderedTableRow const &row) {
            Element element = row.row;
            return std::move(element).environment<TableLayoutContextKey>(layout);
        },
        0.f,
        Alignment::Stretch
    )};

    Element bodyElement = scrollBody
                              ? Element {ScrollView {
                                    .axis = ScrollAxis::Vertical,
                                    .children = children(std::move(bodyContent)),
                                }}
                                    .flex(1.f, 1.f, 0.f)
                              : std::move(bodyContent);

    std::vector<Element> childrenList;
    childrenList.reserve(header ? 3u : 1u);
    if (header) {
        Element headerRow = *header;
        if (!items.empty()) {
            headerRow = decorateHeader(std::move(headerRow), columns, theme(), sortColumn, sortAscending);
        }
        childrenList.push_back(std::move(headerRow).environment<TableLayoutContextKey>(layout));
        if (showDividers && (!items.empty() || !rows.empty())) {
            childrenList.push_back(divider(resolved.dividerColor, resolved.dividerInsetH));
        }
    }
    childrenList.push_back(std::move(bodyElement));

    return VStack {
        .spacing = 0.f,
        .alignment = Alignment::Stretch,
        .children = std::move(childrenList),
    }
        .fill(FillStyle::solid(resolved.backgroundColor));
}

} // namespace lambdaui
