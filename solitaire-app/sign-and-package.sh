#!/bin/bash
set -euo pipefail

if [[ "$#" -ne 5 ]]; then
  echo "Usage: $0 <app-bundle> <app-sign-id> <installer-sign-id> <out-pkg> <entitlements>" >&2
  exit 1
fi

APP="$1"
SIGN_ID="$2"
INSTALLER_ID="$3"
OUT_PKG="$4"
ENTITLEMENTS="$5"

if [[ ! -d "${APP}" ]]; then
  echo "App bundle not found: ${APP}" >&2
  exit 1
fi
if [[ -z "${SIGN_ID}" || -z "${INSTALLER_ID}" ]]; then
  echo "Set LAMBDA_SIGN_APP_ID and LAMBDA_SIGN_INSTALLER_ID before running this target." >&2
  exit 1
fi
if [[ ! -f "${ENTITLEMENTS}" ]]; then
  echo "Entitlements file not found: ${ENTITLEMENTS}" >&2
  exit 1
fi

codesign --force --timestamp \
  --entitlements "${ENTITLEMENTS}" \
  --sign "${SIGN_ID}" \
  "${APP}"

codesign --verify --strict --verbose=2 "${APP}"

productbuild --component "${APP}" /Applications \
  --sign "${INSTALLER_ID}" "${OUT_PKG}"
