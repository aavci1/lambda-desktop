#pragma once

/// \file Lambda.hpp
///
/// Primary umbrella include for Lambda: application/window, core types, reactive primitives, and 2D
/// graphics (Canvas, styles). Prefer including specific `<Lambda/...>` headers when you only need a
/// subset to reduce compile time.

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Cursor.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/PopupMenu.hpp>
#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/Reactive/Reactive.hpp>
#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/Graphics/SvgPath.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/Graphics/VulkanContext.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/ImageNode.hpp>
#include <Lambda/SceneGraph/PathNode.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneInteraction.hpp>
#include <Lambda/SceneGraph/SceneRenderer.hpp>
#include <Lambda/SceneGraph/SceneTraversal.hpp>
#include <Lambda/SceneGraph/TextNode.hpp>
