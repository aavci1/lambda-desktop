#include <Lambda/UI/Detail/LayoutDebugDump.hpp>

#include <Lambda/Debug/DebugFlags.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/TextNode.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace lambdaui {

namespace {

thread_local std::size_t gMeasureCount = 0;
thread_local scenegraph::SceneGraph const* gAttachedSceneGraph = nullptr;

void printIndent(int depth) {
  for (int i = 0; i < depth; ++i) {
    std::fputs("  ", stderr);
  }
}

std::string formatLocalId(LocalId const& local) {
  char buffer[64];
  if (local.kind == LocalId::Kind::Positional) {
    std::snprintf(buffer, sizeof(buffer), "i:%llu",
                  static_cast<unsigned long long>(local.value == 0 ? 0ull : local.value - 1ull));
  } else {
    std::snprintf(buffer, sizeof(buffer), "k:%016llx", static_cast<unsigned long long>(local.value));
  }
  return std::string(buffer);
}

std::string formatComponentKey(ComponentKey const& key) {
  if (key.empty()) {
    return "<root>";
  }
  std::string out;
  std::vector<LocalId> const ids = key.materialize();
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i != 0) {
      out += '/';
    }
    out += formatLocalId(ids[i]);
  }
  return out;
}

void dumpSceneNode(scenegraph::SceneNode const& node, int depth) {
  std::string extra;
  if (node.kind() == scenegraph::SceneNodeKind::Text) {
    auto const& textNode = static_cast<scenegraph::TextNode const&>(node);
    if (textNode.layout()) {
      extra = " textLayout";
      if (textNode.layout()->measuredSize.width > 0.f || textNode.layout()->measuredSize.height > 0.f) {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), " layout=(%.1f, %.1f)",
                      static_cast<double>(textNode.layout()->measuredSize.width),
                      static_cast<double>(textNode.layout()->measuredSize.height));
        extra += buffer;
      }
    }
  }
  printIndent(depth);
  std::fprintf(stderr,
               "[lambda:layout] node kind=%.*s pos=(%.1f, %.1f) bounds=(%.1f, %.1f, %.1f, %.1f)%s%s\n",
               static_cast<int>(scenegraph::sceneNodeKindName(node.kind()).size()),
               scenegraph::sceneNodeKindName(node.kind()).data(),
               static_cast<double>(node.position().x), static_cast<double>(node.position().y),
               static_cast<double>(node.bounds().x), static_cast<double>(node.bounds().y),
               static_cast<double>(node.bounds().width), static_cast<double>(node.bounds().height),
               node.interaction() ? " interactive" : "",
               extra.c_str());
  for (std::unique_ptr<scenegraph::SceneNode> const& child : node.children()) {
    dumpSceneNode(*child, depth + 1);
  }
}

} // namespace

bool layoutDebugEnabled() {
  return debug::layoutEnabled();
}

void layoutDebugBeginPass() {
  if (!layoutDebugEnabled()) {
    return;
  }
  gMeasureCount = 0;
  std::fprintf(stderr, "[lambda:layout] --- rebuild ---\n");
}

void layoutDebugEndPass() {
  if (!layoutDebugEnabled()) {
    return;
  }
  std::fprintf(stderr, "[lambda:layout] --- end ---\n");
}

namespace detail {

void layoutDebugRecordMeasureSlow(LayoutConstraints const&, Size) {
  ++gMeasureCount;
}

} // namespace detail

void layoutDebugDumpRetained(scenegraph::SceneGraph const& graph) {
  if (!layoutDebugEnabled()) {
    return;
  }

  std::fprintf(stderr, "[lambda:layout] scene graph:\n");
  dumpSceneNode(graph.root(), 0);

  std::vector<std::pair<ComponentKey, Rect>> entries = graph.snapshotGeometry();
  std::sort(entries.begin(), entries.end(), [](auto const& lhs, auto const& rhs) {
    ComponentKey const& a = lhs.first;
    ComponentKey const& b = rhs.first;
    std::vector<LocalId> const aIds = a.materialize();
    std::vector<LocalId> const bIds = b.materialize();
    std::size_t const common = std::min(aIds.size(), bIds.size());
    for (std::size_t i = 0; i < common; ++i) {
      if (aIds[i].kind != bIds[i].kind) {
        return static_cast<int>(aIds[i].kind) < static_cast<int>(bIds[i].kind);
      }
      if (aIds[i].value != bIds[i].value) {
        return aIds[i].value < bIds[i].value;
      }
    }
    return aIds.size() < bIds.size();
  });

  std::fprintf(stderr, "[lambda:layout] geometry entries=%zu\n", entries.size());
  for (auto const& [key, rect] : entries) {
    std::string const keyText = formatComponentKey(key);
    std::fprintf(stderr, "[lambda:layout]   key=%s frame=(%.1f, %.1f, %.1f, %.1f)\n", keyText.c_str(),
                 static_cast<double>(rect.x), static_cast<double>(rect.y), static_cast<double>(rect.width),
                 static_cast<double>(rect.height));
  }

  std::fprintf(stderr, "[lambda:layout] measure calls=%zu\n", gMeasureCount);
}

void layoutDebugAttachSceneGraph(scenegraph::SceneGraph const* graph) {
  gAttachedSceneGraph = graph;
}

void layoutDebugDumpAttached(char const* reason) {
  if (!layoutDebugEnabled() || !gAttachedSceneGraph) {
    return;
  }
  layoutDebugBeginPass();
  if (reason && *reason) {
    std::fprintf(stderr, "[lambda:layout] reason=%s\n", reason);
  }
  layoutDebugDumpRetained(*gAttachedSceneGraph);
  layoutDebugEndPass();
}

} // namespace lambdaui
