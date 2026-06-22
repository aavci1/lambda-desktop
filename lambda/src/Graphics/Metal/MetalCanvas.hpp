#pragma once

#include <Lambda/Graphics/Canvas.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace lambda {

class Image;
struct MetalRenderTargetSpec;
class TextSystem;
class Window;
struct MetalFrameRecorder;
struct MetalRecorderSlice;

/// Creates the Metal-backed canvas for a window (macOS only).
std::unique_ptr<Canvas> createMetalCanvas(Window* window, void* caMetalLayer, unsigned int handle,
                                          TextSystem& textSystem,
                                          std::function<void()> requestRedraw = {});

/// Creates a Metal canvas that renders into a caller-owned texture instead of a CAMetalLayer.
std::unique_ptr<Canvas> createMetalRenderTargetCanvas(MetalRenderTargetSpec const& spec,
                                                      TextSystem& textSystem);

/// When `sync` is true, the next `present()` uses commit → waitUntilScheduled → drawable present (resize-safe).
/// No-op if `canvas` is not a Metal-backed canvas.
void setSyncPresentForCanvas(Canvas* canvas, bool sync);

/// Waits for the most recently submitted Metal frame for this canvas to complete on the GPU.
/// No-op if `canvas` is not a Metal-backed canvas or no frame has been submitted yet.
void waitForCanvasLastPresentComplete(Canvas* canvas);

/// Starts recording Metal draw ops into `target` instead of the live frame.
/// Returns false if `canvas` is not Metal-backed.
bool beginRecordedOpsCaptureForCanvas(Canvas* canvas, MetalFrameRecorder* target);

/// Stops Metal draw-op recording started by `beginRecordedOpsCaptureForCanvas`.
void endRecordedOpsCaptureForCanvas(Canvas* canvas);

/// Replays a slice of cached Metal draw data into the current live frame.
void replayRecordedOpsForCanvas(Canvas* canvas, MetalFrameRecorder const& recorded, MetalRecorderSlice const& slice);

/// Replays a slice of cached local-space Metal draw data using the canvas's current translation and clip state.
/// Returns false when the canvas cannot replay the slice in the current state.
bool replayRecordedLocalOpsForCanvas(Canvas* canvas, MetalFrameRecorder const& recorded, MetalRecorderSlice const& slice);

/// Returns the active Metal canvas DPI scale, or 1 when the canvas is not Metal-backed.
float dpiScaleForCanvas(Canvas* canvas);

/// Requests a CPU readback of the next presented Metal frame.
bool requestNextFrameCaptureForCanvas(Canvas* canvas);

/// Retrieves the most recent captured frame bytes (BGRA8). Returns false if no capture is available.
bool takeCapturedFrameForCanvas(Canvas* canvas, std::vector<std::uint8_t>& out, std::uint32_t& width,
                                std::uint32_t& height);

} // namespace lambda
