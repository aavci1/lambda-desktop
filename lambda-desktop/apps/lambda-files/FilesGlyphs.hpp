#pragma once

#include "FilesStore.hpp"
#include "FilesTheme.hpp"
#include "FilesTrace.hpp"

#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/ImageFillMode.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/Views/Image.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace lambda_files {

namespace detail {

inline std::shared_ptr<lambdaui::Image> themedFileIconImage(std::string const& path) {
  if (path.empty()) return nullptr;
  double const startMs = trace::nowMs();
  static std::unordered_map<std::string, std::shared_ptr<lambdaui::Image>> cache;
  auto found = cache.find(path);
  if (found != cache.end()) {
    LAMBDA_FILES_TRACE_EVENT("icon-image cache-hit path=\"%s\" elapsed=%.3fms\n",
                 path.c_str(),
                 trace::nowMs() - startMs);
    return found->second;
  }
  auto image = lambdaui::loadImage(path);
  cache.emplace(path, image);
  LAMBDA_FILES_TRACE_EVENT("icon-image load path=\"%s\" ok=%d elapsed=%.3fms\n",
               path.c_str(),
               image ? 1 : 0,
               trace::nowMs() - startMs);
  return image;
}

} // namespace detail

struct FolderGlyph {
  lambdaui::Element body() const {
    using namespace lambdaui;
    return ZStack{
        .horizontalAlignment = Alignment::Start,
        .verticalAlignment = Alignment::Start,
        .children = children(
            Rectangle{}
                .size(28.f, 10.f)
                .position(6.f, 0.f)
                .fill(FillStyle::linearGradient(FilesTheme::folderA, FilesTheme::folderB, {0.f, 0.f}, {0.f, 1.f}))
                .cornerRadius(CornerRadius{6.f, 6.f, 0.f, 0.f}),
            Rectangle{}
                .size(72.f, 52.f)
                .position(0.f, 6.f)
                .fill(FillStyle::linearGradient(FilesTheme::folderA, FilesTheme::folderB, {0.f, 0.f}, {0.f, 1.f}))
                .cornerRadius(8.f)
                .stroke(StrokeStyle::solid(Color{1.f, 1.f, 1.f, 0.4f}, 1.f)),
            Icon{.name = IconName::FolderOpen, .size = 22.f, .color = Color{1.f, 1.f, 1.f, 0.95f}}
                .position(25.f, 28.f)),
    }
        .size(72.f, 58.f);
  }
};

struct FileDocGlyph {
  FileVisualKind kind = FileVisualKind::Generic;

  lambdaui::Element body() const {
    using namespace lambdaui;
    Color previewTop = Color::hex(0x4D94FF);
    Color previewBottom = Color::hex(0x99C2FF);
    if (kind == FileVisualKind::Pdf || kind == FileVisualKind::Sketch) {
      previewTop = Color::hex(0xF3F5FA);
      previewBottom = Color::hex(0xE3E8F2);
    } else if (kind == FileVisualKind::Image) {
      previewTop = Color::hex(0x7EB6FF);
      previewBottom = Color::hex(0xC8DCFF);
    } else if (kind == FileVisualKind::Presentation) {
      previewTop = Color::hex(0x66A3FF);
      previewBottom = Color::hex(0xB8D4FF);
    }

    std::vector<Element> lineLayers;
    lineLayers.push_back(Rectangle{}.size(42.f, 2.f).position(6.f, 10.f).fill(FilesTheme::fileLine).cornerRadius(2.f));
    lineLayers.push_back(Rectangle{}.size(34.f, 2.f).position(6.f, 15.f).fill(FilesTheme::fileLine).cornerRadius(2.f));
    lineLayers.push_back(Rectangle{}.size(38.f, 2.f).position(6.f, 20.f).fill(FilesTheme::fileLine).cornerRadius(2.f));

    return ZStack{
        .children = children(
            Rectangle{}
                .size(54.f, 66.f)
                .fill(FillStyle::linearGradient(FilesTheme::filePaperTop, FilesTheme::filePaperBottom, {0.f, 0.f},
                                                 {0.f, 1.f}))
                .cornerRadius(6.f)
                .stroke(StrokeStyle::solid(Color{0.08f, 0.12f, 0.24f, 0.06f}, 0.5f)),
            Rectangle{}.size(14.f, 14.f).position(40.f, 0.f).fill(Color::hex(0xDDE5F0)),
            ZStack{.children = std::move(lineLayers)},
            Rectangle{}
                .size(42.f, 28.f)
                .position(6.f, 32.f)
                .fill(FillStyle::linearGradient(previewTop, previewBottom, {0.f, 0.f}, {0.f, 1.f}))
                .cornerRadius(3.f)),
    }
        .size(54.f, 66.f);
  }
};

struct FileItemTile {
  FileEntry entry;
  std::string iconPath;
  lambdaui::Reactive::Bindable<bool> selected{false};
  std::function<void(lambdaui::Modifiers)> onActivate;
  std::function<void()> onContextMenu;

  lambdaui::Element body() const {
    using namespace lambdaui;
    auto hover = useHover();
    lambdaui::Reactive::Bindable<bool> const selectedBinding = selected;

    lambdaui::Reactive::Bindable<FillStyle> const tileFill{[hover, selectedBinding] {
      if (selectedBinding.evaluate()) {
        return FillStyle::solid(FilesTheme::selectFill);
      }
      if (hover()) {
        return FillStyle::solid(FilesTheme::hoverFill);
      }
      return FillStyle::solid(Colors::transparent);
    }};
    lambdaui::Reactive::Bindable<StrokeStyle> const tileStroke{[selectedBinding] {
      if (selectedBinding.evaluate()) {
        return StrokeStyle::solid(FilesTheme::selectBorder, 1.f);
      }
      return StrokeStyle::solid(Colors::transparent, 0.f);
    }};

    lambdaui::Element glyph = [this] {
      if (auto image = detail::themedFileIconImage(iconPath)) {
        return lambdaui::Element{lambdaui::views::Image{
                   .source = std::move(image),
                   .fillMode = lambdaui::ImageFillMode::Fit,
               }}
            .size(70.f, 66.f);
      }
      return entry.isDirectory ? lambdaui::Element{FolderGlyph{}} : lambdaui::Element{FileDocGlyph{.kind = entry.visualKind}};
    }();

    auto tile = VStack{
                   .spacing = 6.f,
                   .alignment = Alignment::Center,
                   .children = children(
                       std::move(glyph),
                       Text{
                           .text = gridDisplayName(entry.name),
                           .font = Font{.size = 12.f, .weight = 500.f},
                           .color = FilesTheme::text,
                           .horizontalAlignment = HorizontalAlignment::Center,
                           .wrapping = TextWrapping::Wrap,
                           .maxLines = 2,
                       }
                           .width(FilesTheme::kGridMinCell - 8.f),
                       Text{
                           .text = formatEntrySubtitle(entry),
                           .font = Font{.size = 10.5f},
                           .color = FilesTheme::text3,
                           .horizontalAlignment = HorizontalAlignment::Center,
                           .maxLines = 1,
                       }
                           .width(FilesTheme::kGridMinCell - 8.f))}
               .padding(8.f, 4.f, 8.f, 4.f)
               .width(FilesTheme::kGridMinCell)
               .fill(tileFill)
               .stroke(tileStroke)
               .cornerRadius(9.f)
               .clipContent(true);
    if (onActivate || onContextMenu) {
      auto activate = onActivate;
      auto contextMenu = onContextMenu;
      tile = std::move(tile).onTap(
          [activate, contextMenu](lambdaui::MouseButton button, lambdaui::Modifiers modifiers) {
        if (button == lambdaui::MouseButton::Left && activate) {
          activate(modifiers);
        } else if (button == lambdaui::MouseButton::Right && contextMenu) {
          contextMenu();
        }
      });
    }
    return tile;
  }
};

} // namespace lambda_files
