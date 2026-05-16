#!/bin/sh
# Build a wasm shader. Optional --export <out_dir> additionally copies
# parser modules (basename "parser") into <out_dir> for bundling into
# an explorer --contract_rich_parser_folder. No-op for non-parser targets.
#
# Usage:
#   make_shader.sh <name>                     # build only
#   make_shader.sh --export <out_dir> <name>  # build, then export if parser

CLANG="${CLANG:-/opt/homebrew/opt/llvm/bin/clang}"

EXPORT_DIR=""
if [ "$1" = "--export" ]; then
    EXPORT_DIR="$2"
    shift 2
fi

NAME="$1"

"$CLANG" -O3 --target=wasm32 -std=c++17 -fno-rtti \
    -Wl,--export-dynamic,--no-entry,--allow-undefined \
    -nostdlib "$NAME.cpp" --output "$NAME.wasm"

if [ -n "$EXPORT_DIR" ]; then
    case "$NAME" in
        */parser)
            mkdir -p "$EXPORT_DIR"
            BASENAME=$(dirname "$NAME")
            cp "$NAME.wasm" "$EXPORT_DIR/$BASENAME.parser.wasm"
            ;;
    esac
fi
