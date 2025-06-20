#!/bin/bash

# VTR Interposer Build Script
# This script simplifies building VTR with interposer support

set -e  # Exit on any error

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BUILD_TYPE="Release"
ENABLE_INTERPOSER="ON"
JOBS=$(nproc)
VERBOSE=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print help
print_help() {
    echo "VTR Interposer Build Script"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help              Show this help message"
    echo "  -d, --debug             Build in Debug mode (default: Release)"
    echo "  -r, --release           Build in Release mode"
    echo "  --disable-interposer    Build without interposer support"
    echo "  --build-dir DIR         Specify build directory (default: ./build)"
    echo "  -j, --jobs N            Number of parallel jobs (default: $(nproc))"
    echo "  -v, --verbose           Verbose output"
    echo "  --clean                 Clean build directory before building"
    echo ""
    echo "Examples:"
    echo "  $0                      # Standard release build with interposer support"
    echo "  $0 --debug              # Debug build"
    echo "  $0 --disable-interposer # Build without interposer support"
    echo "  $0 --clean --debug      # Clean debug build"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            print_help
            exit 0
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        --disable-interposer)
            ENABLE_INTERPOSER="OFF"
            shift
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            print_help
            exit 1
            ;;
    esac
done

# Print configuration
echo -e "${BLUE}=== VTR Interposer Build Configuration ===${NC}"
echo -e "Build Type:        ${YELLOW}${BUILD_TYPE}${NC}"
echo -e "Interposer Support: ${YELLOW}${ENABLE_INTERPOSER}${NC}"
echo -e "Build Directory:   ${YELLOW}${BUILD_DIR}${NC}"
echo -e "Parallel Jobs:     ${YELLOW}${JOBS}${NC}"
echo ""

# Clean if requested
if [[ ${CLEAN:-0} -eq 1 ]]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "${BUILD_DIR}"
fi

# Create build directory
echo -e "${BLUE}Creating build directory...${NC}"
mkdir -p "${BUILD_DIR}"

# Navigate to build directory
cd "${BUILD_DIR}"

# Configure with CMake
echo -e "${BLUE}Configuring with CMake...${NC}"
CMAKE_ARGS=(
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DVTR_ENABLE_INTERPOSER_SUPPORT=${ENABLE_INTERPOSER}"
)

if [[ ${VERBOSE} -eq 1 ]]; then
    CMAKE_ARGS+=("-DCMAKE_VERBOSE_MAKEFILE=ON")
fi

echo -e "${YELLOW}CMake command: cmake ${CMAKE_ARGS[*]} ..${NC}"
cmake "${CMAKE_ARGS[@]}" ..

if [[ $? -ne 0 ]]; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi

# Build
echo -e "${BLUE}Building VTR with ${JOBS} parallel jobs...${NC}"
if [[ ${VERBOSE} -eq 1 ]]; then
    make -j${JOBS} VERBOSE=1
else
    make -j${JOBS}
fi

if [[ $? -ne 0 ]]; then
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

# Success message
echo ""
echo -e "${GREEN}=== Build Completed Successfully! ===${NC}"
echo -e "Build directory: ${BUILD_DIR}"
echo -e "VPR executable: ${BUILD_DIR}/vpr/vpr"
echo ""

# Show basic usage if interposer is enabled
if [[ "${ENABLE_INTERPOSER}" == "ON" ]]; then
    echo -e "${BLUE}=== Interposer Usage Examples ===${NC}"
    echo ""
    echo "Basic interposer run:"
    echo "  ./vpr/vpr arch.xml design.blif \\"
    echo "    --percent_wires_cut 50 \\"
    echo "    --num_cuts 2 \\"
    echo "    --delay_increase 150"
    echo ""
    echo "Advanced interposer run:"
    echo "  ./vpr/vpr arch.xml design.blif \\"
    echo "    --percent_wires_cut 75 \\"
    echo "    --num_cuts 3 \\"
    echo "    --delay_increase 200 \\"
    echo "    --allow_chanx_interposer_connections on \\"
    echo "    --transfer_interposer_fanins on"
    echo ""
    echo "Pre-packing only (fast exploration):"
    echo "  ./vpr/vpr arch.xml design.blif \\"
    echo "    --pre_pack_only on \\"
    echo "    --percent_wires_cut 60 \\"
    echo "    --num_cuts 2"
    echo ""
fi

echo -e "${GREEN}Happy experimenting with VTR Interposer!${NC}" 