#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

LAMBDA_INCLUDE_DIR="lambda/include"
LAMBDA_SOURCE_DIR="lambda/src"

check_module() {
  local name="$1"
  local path="$2"
  local allowed="$3"
  local violations

  violations="$(grep -Rnh '^#include <Lambda/' "$path" | grep -v "$allowed" || true)"
  if [[ -n "$violations" ]]; then
    printf '%s module has upward or cross-layer includes:\n%s\n' "$name" "$violations" >&2
    return 1
  fi
}

check_source_module() {
  local name="$1"
  local path="$2"
  local allowed="$3"
  local violations

  if [[ ! -d "$path" ]]; then
    return 0
  fi

  violations="$(
    grep -RnE '^#include[[:space:]]+[<"]((Lambda/)?(Detail|Core|Reactive|Graphics|SceneGraph|Layout|UI)/)' "$path" |
      grep -Ev "$allowed" || true
  )"
  if [[ -n "$violations" ]]; then
    printf '%s sources have upward or cross-layer includes:\n%s\n' "$name" "$violations" >&2
    return 1
  fi
}

check_module "Detail" "$LAMBDA_INCLUDE_DIR/Lambda/Detail/" 'Lambda/Detail/'
check_module "Core" "$LAMBDA_INCLUDE_DIR/Lambda/Core/" 'Lambda/Core/\|Lambda/Detail/'
check_module "Reactive" "$LAMBDA_INCLUDE_DIR/Lambda/Reactive/" 'Lambda/Core/\|Lambda/Detail/\|Lambda/Reactive/'
check_module "Graphics" "$LAMBDA_INCLUDE_DIR/Lambda/Graphics/" 'Lambda/Core/\|Lambda/Detail/\|Lambda/Graphics/'
check_module "SceneGraph" "$LAMBDA_INCLUDE_DIR/Lambda/SceneGraph/" 'Lambda/Core/\|Lambda/Reactive/\|Lambda/Graphics/\|Lambda/Detail/\|Lambda/SceneGraph/'
check_module "Layout" "$LAMBDA_INCLUDE_DIR/Lambda/Layout/" 'Lambda/Core/\|Lambda/SceneGraph/\|Lambda/Detail/\|Lambda/Layout/'

check_source_module "Core" "$LAMBDA_SOURCE_DIR/Core/" '(<Lambda/(Core|Detail)/)|"(Core|Detail)/'
check_source_module "Reactive" "$LAMBDA_SOURCE_DIR/Reactive/" '(<Lambda/(Core|Detail|Reactive)/)|"(Core|Detail|Reactive)/'
check_source_module "Graphics" "$LAMBDA_SOURCE_DIR/Graphics/" '(<Lambda/(Core|Detail|Graphics)/)|"(Core|Detail|Graphics)/'
check_source_module "SceneGraph" "$LAMBDA_SOURCE_DIR/SceneGraph/" '(<Lambda/(Core|Reactive|Graphics|Detail|SceneGraph)/)|"(Core|Reactive|Graphics|Detail|SceneGraph)/'
check_source_module "Layout" "$LAMBDA_SOURCE_DIR/Layout/" '(<Lambda/(Core|SceneGraph|Detail|Layout)/)|"(Core|SceneGraph|Detail|Layout)/'
