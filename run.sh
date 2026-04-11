#!/bin/bash
# ──────────────────────────────────────────────────────────────────────────────
# CrystalClockVK — Development Launcher
# Sets up Vulkan environment (MoltenVK ICD, validation layers) and runs
# the binary with all output tee'd to bin/output.log.
# ──────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="${SCRIPT_DIR}/bin"
BINARY="${BIN_DIR}/CrystalClockVK"
LOG_FILE="${BIN_DIR}/output.log"

# ── Vulkan SDK ────────────────────────────────────────────────────────────────
VULKAN_SDK="/Users/jeanxpereira/VulkanSDK/1.4.341.1/macOS"

export VULKAN_SDK
export PATH="${VULKAN_SDK}/bin:${PATH}"
export DYLD_LIBRARY_PATH="${VULKAN_SDK}/lib:${DYLD_LIBRARY_PATH:-}"
export VK_ICD_FILENAMES="${VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json"
export VK_LAYER_PATH="${VULKAN_SDK}/share/vulkan/explicit_layer.d"

# ── Validation layer settings ─────────────────────────────────────────────────
# Uncomment to enable GPU-Assisted Validation (slower but catches more bugs):
# export VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT
# Uncomment to disable validation layers entirely (for perf testing):
# export VK_LOADER_LAYERS_DISABLE=*validation*

# ── Build if needed ───────────────────────────────────────────────────────────
if [[ ! -f "${BINARY}" ]] || [[ "${1:-}" == "--build" ]]; then
    echo "▸ Building..."
    cmake --build "${SCRIPT_DIR}/build" 2>&1
    echo ""
fi

# ── Run ───────────────────────────────────────────────────────────────────────
if [[ ! -f "${BINARY}" ]]; then
    echo "✗ Binary not found at ${BINARY}"
    echo "  Run: cmake --preset default && cmake --build build"
    exit 1
fi

echo "▸ Launching CrystalClockVK"
echo "  Binary:  ${BINARY}"
echo "  Log:     ${LOG_FILE}"
echo "  ICD:     MoltenVK (${VULKAN_SDK})"
echo ""

# Run binary, mirror output to terminal AND log file
"${BINARY}" "$@" 2>&1 | tee "${LOG_FILE}"
EXIT_CODE=${PIPESTATUS[0]}

echo ""
echo "▸ Exit code: ${EXIT_CODE}"
echo "▸ Log saved: ${LOG_FILE}"
exit ${EXIT_CODE}
