#!/bin/sh
# Build every shader. Optional --export [out_dir] also collects per-contract
# parser modules into a single folder ready for an explorer's
# --contract_rich_parser_folder. Default out_dir: Explorer/modules/
#
# Usage:
#   make_all.sh                  # build only
#   make_all.sh --export         # build, export to Explorer/modules/
#   make_all.sh --export <path>  # build, export to <path>
set -e
cd "$(dirname "$0")"

EXPORT_FLAGS=""
if [ "$1" = "--export" ]; then
    OUT_DIR="${2:-Explorer/modules}"
    EXPORT_FLAGS="--export $OUT_DIR"
    mkdir -p "$OUT_DIR"
    find "$OUT_DIR" -name '*.wasm' -delete
fi

build() {
    ./make_shader.sh $EXPORT_FLAGS "$1"
}

build amm/app
build amm/contract
build amm/parser
build asset_man/app
build asset_man/contract
build bans/app
build bans/contract
build bans/parser
build blackhole/app
build blackhole/contract
build blackhole/parser
build aphorize/contract
build dao-accumulator/app
build dao-accumulator/contract
build dao-accumulator/parser
build dao-core/app
build dao-core/app-admin
build dao-core/contract
build dao-core/parser
build dao-core-masternet/app
build dao-core-masternet/app-admin
build dao-core-masternet/contract
build dao-core-testnet/app
build dao-core-testnet/app-admin
build dao-core-testnet/contract
build dao-core2/app
build dao-core2/contract
build dao-core2/parser
build dao-vote/app
build dao-vote/contract
build dao-vote/parser
build dao-vault/app
build dao-vault/contract
build dao-vault/parser
build dummy/app
build dummy/contract
build faucet/app
build faucet/contract
build faucet/parser
build faucet2/app
build faucet2/contract
build faucet2/parser
build fuddle/contract
build gallery/app
build gallery/app-admin
build gallery/contract
build gallery/parser
build minter/app
build minter/contract
build minter/parser
build mirrorcoin/app
build mirrorcoin/contract
build nephrite/app
build nephrite/contract
build nephrite/parser
build oracle/contract
build oracle2/app
build oracle2/contract
build oracle2/parser
build perpetual/app
build perpetual/contract
build pipe/contract
build playground/app
build playground/contract
build profit_pool/app
build profit_pool/contract
build roulette/app
build roulette/contract
build sidechain/contract
build sidechain_pos/parser
build pbft/parser
build StableCoin/contract
build upgradable/contract
build upgradable2/contract
build upgradable2/Test/test_app
build upgradable2/Test/test_v0
build upgradable2/Test/test_v1
build upgradable3/Test/test_app
build upgradable3/Test/test_v0
build upgradable3/Test/test_v1
build upgradable3/Test/test_v0_migrate
build vault/app
build vault/contract
build vault/parser
build vault_anon/app
build vault_anon/contract
build vault_anon/parser
build voting/app
build voting/contract
