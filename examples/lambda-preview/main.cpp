#include <Flux.hpp>
#include <Flux/Graphics/Image.hpp>
#include <Flux/UI/Shortcut.hpp>
#include <Flux/UI/Theme.hpp>
#include <Flux/UI/UI.hpp>
#include <Flux/UI/Views/Views.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

using namespace flux;

namespace {

struct ImageLoadResult {
  std::shared_ptr<flux::Image> image;
  Size size;
  std::string status;
};

ImageLoadResult openImage(std::string const& pathText) {
  if (pathText.empty()) {
    return {.status = "Enter an image path to open."};
  }
  std::filesystem::path const path(pathText);
  auto image = loadImage(path.string());
  if (!image) {
    return {.status = "Could not open " + path.string()};
  }
  Size const size = image->size();
  return {
      .image = std::move(image),
      .size = size,
      .status = "Opened " + path.string() + " (" + std::to_string(static_cast<int>(std::round(size.width))) + " x " +
                std::to_string(static_cast<int>(std::round(size.height))) + ")",
  };
}

float clampZoom(float value) {
  return std::clamp(value, 0.10f, 8.0f);
}

std::string zoomLabel(float zoom) {
  return std::to_string(static_cast<int>(std::round(zoom * 100.f))) + "%";
}

std::string fileLabel(std::string const& path) {
  if (path.empty()) return "No image";
  return std::filesystem::path(path).filename().string();
}

struct LambdaPreview {
  std::string initialPath;
  std::shared_ptr<flux::Image> initialImage;
  Size initialImageSize;
  std::string initialStatus;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    auto path = useState(initialPath);
    auto image = useState(initialImage);
    auto imageSize = useState(initialImageSize);
    auto zoom = useState(1.f);
    auto status = useState(initialStatus.empty() ? std::string{"Ready"} : initialStatus);
    auto scrollOffset = useState(Point{});

    auto loadCurrent = [path, image, imageSize, zoom, status, scrollOffset] {
      ImageLoadResult result = openImage(path.peek());
      status.set(result.status);
      if (result.image) {
        image.set(std::move(result.image));
        imageSize.set(result.size);
        zoom.set(1.f);
        scrollOffset.set(Point{});
      }
    };
    auto zoomBy = [zoom](float factor) {
      zoom.set(clampZoom(zoom.peek() * factor));
    };
    auto resetZoom = [zoom, scrollOffset] {
      zoom.set(1.f);
      scrollOffset.set(Point{});
    };

    TextInput::Style pathStyle;
    pathStyle.font = Font{.size = 13.f, .weight = 450.f};
    pathStyle.height = 34.f;

    Button::Style buttonStyle;
    buttonStyle.font = Font{.size = 12.f, .weight = 650.f};
    buttonStyle.paddingH = 12.f;
    buttonStyle.paddingV = 7.f;
    buttonStyle.cornerRadius = 7.f;

    Element imageContent = Show(
        [image] { return static_cast<bool>(image()); },
        [image, imageSize, zoom] {
          Size const size = imageSize();
          float const z = zoom();
          return VStack{
                     .spacing = 0.f,
                     .alignment = Alignment::Center,
                     .children = children(
                         Element{views::Image{
                             .source = image(),
                             .fillMode = ImageFillMode::Fit,
                         }}
                             .width(std::max(1.f, size.width * z))
                             .height(std::max(1.f, size.height * z)))}
              .padding(32.f);
        },
        [] {
          return VStack{
                     .spacing = 8.f,
                     .alignment = Alignment::Center,
                     .children = children(
                         Text{
                             .text = "No image open",
                             .font = Font{.size = 20.f, .weight = 650.f},
                             .color = Color::primary(),
                             .horizontalAlignment = HorizontalAlignment::Center,
                         },
                         Text{
                             .text = "Enter a PNG, JPEG, SVG, or other supported image path.",
                             .font = Font{.size = 13.f, .weight = 450.f},
                             .color = Color::secondary(),
                             .horizontalAlignment = HorizontalAlignment::Center,
                         })}
              .padding(32.f);
        });

    return VStack{
               .spacing = 0.f,
               .alignment = Alignment::Stretch,
               .children = children(
                   HStack{
                       .spacing = theme().space3,
                       .alignment = Alignment::Center,
                       .children = children(
                           Text{
                               .text = [path] { return fileLabel(path()); },
                               .font = Font{.size = 15.f, .weight = 650.f},
                               .color = Color::primary(),
                               .verticalAlignment = VerticalAlignment::Center,
                           }.width(180.f),
                           TextInput{
                               .value = path,
                               .placeholder = "/path/to/image.png",
                               .style = pathStyle,
                               .onSubmit = [loadCurrent](std::string const&) { loadCurrent(); },
                           }.flex(1.f, 1.f, 0.f),
                           Button{.label = "Open",
                                  .variant = ButtonVariant::Primary,
                                  .style = buttonStyle,
                                  .onTap = loadCurrent},
                           Button{.label = "-",
                                  .variant = ButtonVariant::Secondary,
                                  .disabled = [image] { return !image(); },
                                  .style = buttonStyle,
                                  .onTap = [zoomBy] { zoomBy(0.8f); }},
                           Button{.label = [zoom] { return zoomLabel(zoom()); },
                                  .variant = ButtonVariant::Secondary,
                                  .disabled = [image] { return !image(); },
                                  .style = buttonStyle,
                                  .onTap = resetZoom},
                           Button{.label = "+",
                                  .variant = ButtonVariant::Secondary,
                                  .disabled = [image] { return !image(); },
                                  .style = buttonStyle,
                                  .onTap = [zoomBy] { zoomBy(1.25f); }})}
                       .padding(theme().space3)
                       .fill(FillStyle::solid(Color::windowBackground()))
                       .stroke(StrokeStyle::solid(Color::separator(), 1.f)),
                   ScrollView{
                       .axis = ScrollAxis::Both,
                       .scrollOffset = scrollOffset,
                       .dragScrollEnabled = true,
                       .children = children(std::move(imageContent)),
                   }.flex(1.f, 1.f, 0.f)
                       .fill(FillStyle::solid(Color::controlBackground())),
                   HStack{
                       .spacing = theme().space2,
                       .alignment = Alignment::Center,
                       .children = children(
                           Text{
                               .text = status,
                               .font = Font{.size = 12.f, .weight = 450.f},
                               .color = Color::secondary(),
                               .verticalAlignment = VerticalAlignment::Center,
                           }.flex(1.f, 1.f, 0.f),
                           Text{
                               .text = [imageSize, image] {
                                 if (!image()) return std::string{};
                                 Size const size = imageSize();
                                 return std::to_string(static_cast<int>(std::round(size.width))) + " x " +
                                        std::to_string(static_cast<int>(std::round(size.height)));
                               },
                               .font = Font{.size = 12.f, .weight = 450.f},
                               .color = Color::secondary(),
                               .verticalAlignment = VerticalAlignment::Center,
                           })}
                       .padding(8.f, theme().space3, 8.f, theme().space3)
                       .fill(FillStyle::solid(Color::windowBackground()))
                       .stroke(StrokeStyle::solid(Color::separator(), 1.f)))};
  }
};

} // namespace

int main(int argc, char* argv[]) {
  Application app(argc, argv);

  std::string initialPath = argc > 1 ? std::string(argv[1]) : std::string{};
  ImageLoadResult initial = initialPath.empty() ? ImageLoadResult{} : openImage(initialPath);

  auto& window = app.createWindow<Window>({
      .size = {960.f, 720.f},
      .title = "Lambda Preview",
      .resizable = true,
  });
  window.registerAction("app.quit", {.label = "Quit", .shortcut = shortcuts::Quit, .isEnabled = [] { return true; }});
  window.setView<LambdaPreview>({
      .initialPath = std::move(initialPath),
      .initialImage = std::move(initial.image),
      .initialImageSize = initial.size,
      .initialStatus = initial.status,
  });
  return app.exec();
}
