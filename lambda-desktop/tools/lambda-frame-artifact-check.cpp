#include <Lambda/Graphics/Image.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Args {
  std::filesystem::path captureRoot;
  std::filesystem::path traceRoot;
  int backgroundR = 32;
  int backgroundG = 48;
  int backgroundB = 64;
  int colorTolerance = 70;
  int backgroundTolerance = 65;
  int minContentMatches = 24;
  int minExposedSamples = 12;
};

struct SurfaceTrace {
  std::uint64_t id = 0;
  int outputWidth = 0;
  int outputHeight = 0;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int titlebarHeight = 0;
};

struct FrameTrace {
  int outputWidth = 0;
  int outputHeight = 0;
  std::vector<SurfaceTrace> surfaces;
};

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct Pixel {
  int r = 0;
  int g = 0;
  int b = 0;
  int a = 0;
};

[[noreturn]] void fail(std::string const& message) {
  std::fprintf(stderr, "lambda-frame-artifact-check: %s\n", message.c_str());
  std::exit(1);
}

std::vector<std::string> splitCsvLine(std::string const& line) {
  std::vector<std::string> fields;
  std::string field;
  std::stringstream stream(line);
  while (std::getline(stream, field, ',')) {
    fields.push_back(field);
  }
  return fields;
}

int parseInt(std::string const& value, char const* field) {
  char* end = nullptr;
  long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str()) fail(std::string("invalid integer in ") + field + ": " + value);
  return static_cast<int>(parsed);
}

std::uint64_t parseUint64(std::string const& value, char const* field) {
  char* end = nullptr;
  unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
  if (end == value.c_str()) fail(std::string("invalid integer in ") + field + ": " + value);
  return static_cast<std::uint64_t>(parsed);
}

std::filesystem::path findFirst(std::filesystem::path const& root, std::string const& filename) {
  if (!std::filesystem::exists(root)) fail("missing directory: " + root.string());
  for (auto const& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file() && entry.path().filename() == filename) {
      return entry.path();
    }
  }
  fail("missing " + filename + " below " + root.string());
}

std::optional<int> frameIndexFromPng(std::filesystem::path const& path) {
  std::string name = path.filename().string();
  constexpr std::string_view prefix = "frame-";
  constexpr std::string_view suffix = ".png";
  if (!name.starts_with(prefix) || !name.ends_with(suffix)) return std::nullopt;
  std::string raw = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
  char* end = nullptr;
  long parsed = std::strtol(raw.c_str(), &end, 10);
  if (end == raw.c_str() || parsed < 0) return std::nullopt;
  return static_cast<int>(parsed);
}

std::map<int, std::filesystem::path> capturedFrames(std::filesystem::path const& captureRoot) {
  std::map<int, std::filesystem::path> frames;
  if (!std::filesystem::exists(captureRoot)) fail("missing capture root: " + captureRoot.string());
  for (auto const& entry : std::filesystem::recursive_directory_iterator(captureRoot)) {
    if (!entry.is_regular_file()) continue;
    auto index = frameIndexFromPng(entry.path());
    if (!index) continue;
    frames[*index] = entry.path();
  }
  if (frames.empty()) fail("no captured frame PNGs below " + captureRoot.string());
  return frames;
}

std::unordered_map<int, FrameTrace> loadTrace(std::filesystem::path const& traceRoot) {
  std::filesystem::path const tracePath = findFirst(traceRoot, "snap.csv");
  std::ifstream file(tracePath);
  if (!file) fail("failed to open snap trace: " + tracePath.string());

  std::string line;
  if (!std::getline(file, line)) fail("empty snap trace: " + tracePath.string());

  std::unordered_map<int, FrameTrace> frames;
  while (std::getline(file, line)) {
    if (line.empty()) continue;
    auto fields = splitCsvLine(line);
    if (fields.size() < 23) continue;
    int const frame = parseInt(fields[0], "frame");
    std::string const& event = fields[1];
    int const outputWidth = parseInt(fields[4], "output_width");
    int const outputHeight = parseInt(fields[5], "output_height");
    auto& frameTrace = frames[frame];
    frameTrace.outputWidth = outputWidth;
    frameTrace.outputHeight = outputHeight;
    if (event != "surface") continue;
    SurfaceTrace surface{
        .id = parseUint64(fields[9], "surface_id"),
        .outputWidth = outputWidth,
        .outputHeight = outputHeight,
        .x = parseInt(fields[10], "x"),
        .y = parseInt(fields[11], "y"),
        .width = parseInt(fields[12], "width"),
        .height = parseInt(fields[13], "height"),
        .titlebarHeight = parseInt(fields[18], "titlebar_height"),
    };
    if (surface.width > 0 && surface.height > 0) frameTrace.surfaces.push_back(surface);
  }
  if (frames.empty()) fail("snap trace contained no frames: " + tracePath.string());
  return frames;
}

Rect contentRect(SurfaceTrace const& surface) {
  return Rect{surface.x, surface.y, surface.width, surface.height};
}

Rect frameRect(SurfaceTrace const& surface) {
  return Rect{surface.x, surface.y - surface.titlebarHeight, surface.width, surface.height + surface.titlebarHeight};
}

bool contains(Rect rect, double x, double y) {
  return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

Pixel pixelAt(lambda::DecodedImageRgba const& image, int x, int y) {
  x = std::clamp(x, 0, static_cast<int>(image.width) - 1);
  y = std::clamp(y, 0, static_cast<int>(image.height) - 1);
  std::size_t const offset = (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4u;
  return Pixel{
      .r = image.pixels[offset + 0u],
      .g = image.pixels[offset + 1u],
      .b = image.pixels[offset + 2u],
      .a = image.pixels[offset + 3u],
  };
}

Pixel sampleLogical(lambda::DecodedImageRgba const& image, FrameTrace const& trace, double logicalX, double logicalY) {
  double const scaleX = trace.outputWidth > 0 ? static_cast<double>(image.width) / trace.outputWidth : 1.0;
  double const scaleY = trace.outputHeight > 0 ? static_cast<double>(image.height) / trace.outputHeight : 1.0;
  int const x = static_cast<int>(std::lround(logicalX * scaleX));
  int const y = static_cast<int>(std::lround(logicalY * scaleY));
  return pixelAt(image, x, y);
}

int colorDistance(Pixel const& pixel, int r, int g, int b) {
  return std::max({std::abs(pixel.r - r), std::abs(pixel.g - g), std::abs(pixel.b - b)});
}

bool pointCoveredByOtherContent(FrameTrace const& trace, std::uint64_t selfId, double x, double y) {
  for (auto const& other : trace.surfaces) {
    if (other.id == selfId) continue;
    if (contains(contentRect(other), x, y)) return true;
  }
  return false;
}

bool pointNearAnyCurrentFrame(FrameTrace const& trace, double x, double y) {
  for (auto const& surface : trace.surfaces) {
    if (contains(frameRect(surface), x, y)) return true;
  }
  return false;
}

bool parseBackground(std::string const& raw, Args& args) {
  int r = 0;
  int g = 0;
  int b = 0;
  char comma1 = 0;
  char comma2 = 0;
  std::stringstream stream(raw);
  if (!(stream >> r >> comma1 >> g >> comma2 >> b) || comma1 != ',' || comma2 != ',') return false;
  args.backgroundR = std::clamp(r, 0, 255);
  args.backgroundG = std::clamp(g, 0, 255);
  args.backgroundB = std::clamp(b, 0, 255);
  return true;
}

Args parseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto needValue = [&](char const* name) -> std::string {
      if (i + 1 >= argc) fail(std::string("missing value for ") + name);
      return argv[++i];
    };
    if (arg == "--capture-root") {
      args.captureRoot = needValue("--capture-root");
    } else if (arg == "--trace-root") {
      args.traceRoot = needValue("--trace-root");
    } else if (arg == "--background-rgb") {
      if (!parseBackground(needValue("--background-rgb"), args)) fail("invalid --background-rgb, expected R,G,B");
    } else if (arg == "--color-tolerance") {
      args.colorTolerance = parseInt(needValue("--color-tolerance"), "--color-tolerance");
    } else if (arg == "--background-tolerance") {
      args.backgroundTolerance = parseInt(needValue("--background-tolerance"), "--background-tolerance");
    } else if (arg == "--min-content-matches") {
      args.minContentMatches = parseInt(needValue("--min-content-matches"), "--min-content-matches");
    } else if (arg == "--min-exposed-samples") {
      args.minExposedSamples = parseInt(needValue("--min-exposed-samples"), "--min-exposed-samples");
    } else {
      fail("unknown argument: " + arg);
    }
  }
  if (args.captureRoot.empty()) fail("--capture-root is required");
  if (args.traceRoot.empty()) fail("--trace-root is required");
  return args;
}

} // namespace

int main(int argc, char** argv) {
  Args const args = parseArgs(argc, argv);
  auto const frames = capturedFrames(args.captureRoot);
  auto const traces = loadTrace(args.traceRoot);

  int analyzedFrames = 0;
  int framesWithSurfaces = 0;
  int movedFrames = 0;
  int contentSamples = 0;
  int contentMatches = 0;
  int contentMismatches = 0;
  int exposedSamples = 0;
  int exposedBackgroundMatches = 0;
  int exposedBackgroundMismatches = 0;
  std::unordered_map<std::uint64_t, std::vector<SurfaceTrace>> surfaceHistory;

  for (auto const& [frameIndex, path] : frames) {
    auto traceIt = traces.find(frameIndex);
    if (traceIt == traces.end()) continue;
    FrameTrace const& trace = traceIt->second;
    auto image = lambda::decodeImageRgbaFromFile(path.string());
    if (!image) fail("failed to decode captured PNG: " + path.string());
    ++analyzedFrames;
    if (!trace.surfaces.empty()) ++framesWithSurfaces;

    for (auto const& surface : trace.surfaces) {
      constexpr double fractions[] = {0.25, 0.50, 0.75};
      for (double fy : fractions) {
        for (double fx : fractions) {
          double const logicalX = surface.x + (surface.width - 1) * fx;
          double const logicalY = surface.y + (surface.height - 1) * fy;
          if (pointCoveredByOtherContent(trace, surface.id, logicalX, logicalY)) continue;
          int const expectedR =
              surface.width > 1 ? static_cast<int>(std::lround((logicalX - surface.x) * 255.0 / (surface.width - 1))) : 0;
          int const expectedG =
              surface.height > 1 ? static_cast<int>(std::lround((logicalY - surface.y) * 255.0 / (surface.height - 1))) : 0;
          Pixel const pixel = sampleLogical(*image, trace, logicalX, logicalY);
          ++contentSamples;
          if (pixel.a >= 220 && colorDistance(pixel, expectedR, expectedG, 0x90) <= args.colorTolerance) {
            ++contentMatches;
          } else {
            ++contentMismatches;
          }
        }
      }

      auto historyIt = surfaceHistory.find(surface.id);
      if (historyIt == surfaceHistory.end() || historyIt->second.empty()) continue;
      SurfaceTrace const* previousSurface = nullptr;
      int const minDx = std::max(24, surface.width / 4);
      int const minDy = std::max(18, surface.height / 4);
      for (auto const& candidate : historyIt->second) {
        if (std::abs(candidate.x - surface.x) >= minDx || std::abs(candidate.y - surface.y) >= minDy) {
          previousSurface = &candidate;
          break;
        }
      }
      if (!previousSurface) continue;
      SurfaceTrace const& previous = *previousSurface;
      if (previous.x == surface.x && previous.y == surface.y) continue;
      ++movedFrames;
      std::array<double, 3> staleX{0.20, 0.50, 0.80};
      std::array<double, 3> staleY{0.20, 0.50, 0.80};
      if (surface.x > previous.x) {
        staleX = {0.06, 0.14, 0.22};
      } else if (surface.x < previous.x) {
        staleX = {0.78, 0.86, 0.94};
      }
      if (surface.y > previous.y) {
        staleY = {0.08, 0.18, 0.28};
      } else if (surface.y < previous.y) {
        staleY = {0.72, 0.82, 0.92};
      }
      for (double fy : staleY) {
        for (double fx : staleX) {
          double const logicalX = previous.x + (previous.width - 1) * fx;
          double const logicalY = previous.y + (previous.height - 1) * fy;
          if (contains(contentRect(surface), logicalX, logicalY)) continue;
          if (pointNearAnyCurrentFrame(trace, logicalX, logicalY)) continue;
          Pixel const pixel = sampleLogical(*image, trace, logicalX, logicalY);
          ++exposedSamples;
          if (pixel.a >= 220 &&
              colorDistance(pixel, args.backgroundR, args.backgroundG, args.backgroundB) <= args.backgroundTolerance) {
            ++exposedBackgroundMatches;
          } else {
            ++exposedBackgroundMismatches;
          }
        }
      }
    }

    for (auto const& surface : trace.surfaces) {
      auto& history = surfaceHistory[surface.id];
      history.push_back(surface);
      if (history.size() > 8) {
        history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - 8));
      }
    }
  }

  std::printf("lambda-frame-artifact-check: analyzed_frames=%d frames_with_surfaces=%d moved_frames=%d "
              "content_samples=%d content_matches=%d content_mismatches=%d exposed_samples=%d "
              "exposed_background_matches=%d exposed_background_mismatches=%d\n",
              analyzedFrames,
              framesWithSurfaces,
              movedFrames,
              contentSamples,
              contentMatches,
              contentMismatches,
              exposedSamples,
              exposedBackgroundMatches,
              exposedBackgroundMismatches);

  if (analyzedFrames <= 0 || framesWithSurfaces <= 0) fail("no captured frames with trace-backed surfaces");
  if (contentMatches < args.minContentMatches) fail("not enough captured surface-gradient samples matched");
  if (contentMatches <= contentMismatches) fail("captured surface-gradient mismatches exceeded matches");
  if (movedFrames <= 0) fail("no traced moving-surface frames were captured");
  if (exposedSamples < args.minExposedSamples) fail("not enough newly exposed old-surface samples were captured");
  if (exposedBackgroundMismatches > std::max(1, exposedSamples / 10)) {
    fail("newly exposed old-surface samples did not return to background");
  }

  return 0;
}
