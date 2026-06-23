#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneRenderer.hpp>
#include <Lambda/UI/Theme.hpp>

#include "ToggleDemoScene.hpp"

#include <memory>

namespace {

using namespace lambdaui;
using namespace lambdaui::examples::scenegraphdemo;
using namespace lambdaui::scenegraph;

class SceneGraphExampleWindow final : public Window {
  public:
    explicit SceneGraphExampleWindow(WindowConfig const &config)
        : Window(config) {
        m_sceneGraph.setRoot(buildToggleDemoScene(Application::instance().textSystem(), m_theme));
        requestRedraw();
    }

    void render(Canvas &canvas) override {
        canvas.clear(m_theme.windowBackgroundColor);
        if (!m_sceneRenderer) {
            m_sceneRenderer = std::make_unique<SceneRenderer>(canvas);
        }
        m_sceneRenderer->render(m_sceneGraph);
    }

  private:
    Theme m_theme = Theme::light();
    SceneGraph m_sceneGraph;
    std::unique_ptr<SceneRenderer> m_sceneRenderer;
};

} // namespace

int main() {
    Application app;

    app.createWindow<SceneGraphExampleWindow>({
        .size = kWindowSize,
        .title = "Scene Graph",
        .resizable = false,
    });

    return app.exec();
}
