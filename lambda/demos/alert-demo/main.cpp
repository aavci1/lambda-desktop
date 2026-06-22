// Demonstrates Alert + useAlert: informational, confirmation, three-button, Escape, disabled.
#include <Lambda.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/Reactive/Reactive.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Alert.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <string>

using namespace lambda;

struct AlertDemoRoot {
    auto body() const {
        auto theme = useEnvironment<ThemeKey>();
        auto [showAlert, hideAlert, alertOpen] = useAlert();
        (void)alertOpen;

        auto filename = useState(std::string {"report.pdf"});
        auto status = useState(std::string {"Tap a button to open an alert."});

        return ScrollView {
            .axis = ScrollAxis::Vertical,
            .children = children(
                VStack {
                    .spacing = 16.f,
                    .alignment = Alignment::Stretch,
                    .children = children(
                        Text {
                            .text = "Alert demo",
                            .font = Font::largeTitle(),
                            .color = Color::primary()
                        },
                        Text {
                            .text = "Modal alerts via useAlert(). Escape dismisses when enabled. "
                                    "Outside tap does not dismiss.",
                            .font = Font::body(),
                            .color = Color::secondary(),
                            .wrapping = TextWrapping::Wrap
                        },
                        Text {
                            .text = [status] {
                                return status();
                            },
                            .font = Font::footnote(),
                            .color = Color::accent(),
                            .wrapping = TextWrapping::Wrap
                        },

                        Button {
                            .label = "Delete file (confirmation)",
                            .variant = ButtonVariant::Destructive,
                            .onTap = [=] {
                                showAlert(Alert {
                                    .title = "Delete \"" + *filename + "\"?",
                                    .message = "This file will be permanently removed and cannot be recovered.",
                                    .buttons =
                                        {
                                            {.label = "Cancel",
                                             .variant = ButtonVariant::Secondary,
                                             .action = hideAlert},
                                            {.label = "Delete",
                                             .variant = ButtonVariant::Destructive,
                                             .action = [=] {
                                                 status = std::string {"Deleted (simulated)."};
                                             }},
                                        },
                                });
                            },
                        },

                        Button {
                            .label = "Show info (single OK)",
                            .variant = ButtonVariant::Secondary,
                            .onTap = [=] {
                                showAlert(Alert {
                                    .title = "Upload complete",
                                    .message = "\"" + *filename + "\" was uploaded successfully.",
                                });
                            },
                        },

                        Button {
                            .label = "Close document (three buttons)",
                            .variant = ButtonVariant::Ghost,
                            .onTap = [=] {
                                showAlert(Alert {
                                    .title = "Save changes?",
                                    .message = "Your changes will be lost if you don't save.",
                                    .buttons =
                                        {
                                            {.label = "Cancel",
                                             .variant = ButtonVariant::Secondary,
                                             .action = hideAlert},
                                            {.label = "Don't Save",
                                             .variant = ButtonVariant::Ghost,
                                             .action = [=] {
                                                 status = std::string {"Closed without saving."};
                                             }},
                                            {.label = "Save", .variant = ButtonVariant::Primary, .action = [=] {
                                                 status = std::string {"Saved and closed."};
                                             }},
                                        },
                                });
                            },
                        },

                        Button {
                            .label = "Alert with disabled action",
                            .variant = ButtonVariant::Secondary,
                            .onTap = [=] {
                                showAlert(Alert {
                                    .title = "Requires upgrade",
                                    .message = "This action is not available on your plan.",
                                    .buttons =
                                        {
                                            {.label = "Cancel", .variant = ButtonVariant::Secondary, .action = hideAlert},
                                            {.label = "Upgrade",
                                             .variant = ButtonVariant::Primary,
                                             .disabled = true,
                                             .action =
                                                 [=] {
                                                     status = std::string {"Should not run while disabled."};
                                                 }},
                                        },
                                });
                            },
                        }
                    ),
                }
                    .padding(24.f)
            ),
        };
    }
};

int main(int argc, char *argv[]) {
    Application app(argc, argv);
    auto &w = app.createWindow<Window>({
        .size = {800, 800},
        .title = "Lambda — Alert demo",
        .resizable = true,
    });
    w.setView<AlertDemoRoot>();
    return app.exec();
}
