#pragma once

/// \file Flux.hpp
///
/// Primary umbrella include for Flux: application/window, core types, reactive primitives, and 2D
/// graphics (Canvas, styles). Prefer including specific `<Flux/...>` headers when you only need a
/// subset to reduce compile time.

#include <Flux/UI/Application.hpp>
#include <Flux/UI/Cursor.hpp>
#include <Flux/UI/EventQueue.hpp>
#include <Flux/UI/Events.hpp>
#include <Flux/UI/KeyCodes.hpp>
#include <Flux/UI/PopupMenu.hpp>
#include <Flux/Core/Geometry.hpp>
#include <Flux/Core/Color.hpp>
#include <Flux/UI/Window.hpp>
#include <Flux/UI/WindowUI.hpp>
#include <Flux/Reactive/Reactive.hpp>
#include <Flux/Graphics/Canvas.hpp>
#include <Flux/Graphics/Image.hpp>
#include <Flux/Graphics/RenderTarget.hpp>
#include <Flux/Graphics/SvgPath.hpp>
#include <Flux/Graphics/Styles.hpp>
#include <Flux/Graphics/VulkanContext.hpp>
#include <Flux/SceneGraph/SceneNode.hpp>
#include <Flux/SceneGraph/ImageNode.hpp>
#include <Flux/SceneGraph/PathNode.hpp>
#include <Flux/SceneGraph/RectNode.hpp>
#include <Flux/SceneGraph/SceneGraph.hpp>
#include <Flux/SceneGraph/SceneInteraction.hpp>
#include <Flux/SceneGraph/SceneRenderer.hpp>
#include <Flux/SceneGraph/SceneTraversal.hpp>
#include <Flux/SceneGraph/TextNode.hpp>
