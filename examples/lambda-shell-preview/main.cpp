#include "Shell/ShellController.hpp"

#include <Flux.hpp>
#include <Flux/UI/Application.hpp>
#include <Flux/UI/Window.hpp>

namespace {

constexpr float kWindowWidth = 960.f;
constexpr float kWindowHeight = 620.f;

} // namespace

int main(int argc, char* argv[]) {
  flux::Application app(argc, argv);
  app.setName("Lambda Shell Preview");

  lambda_shell::ShellModel model;
  lambda_shell::ShellController controller(app, model);
  model.setPreviewFocus("browser");

  auto& window = app.createWindow<flux::Window>({
      .size = {kWindowWidth, kWindowHeight},
      .title = "Lambda Shell Preview",
      .resizable = false,
  });
  controller.setupPreviewWindow(window, kWindowWidth, kWindowHeight);
  return app.exec();
}
