#!/bin/sh
set -u

UASM="${1:-./build/uasm}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
FAILS=0

trap 'rm -rf "$WORK"' EXIT

if [ ! -x "$UASM" ]; then
    echo "usage: tests/run.sh <path-to-uasm-binary>" >&2
    exit 2
fi

fail() {
    echo "  FAIL $*"
    FAILS=$((FAILS + 1))
}

compile_all() {
    for f in "$ROOT"/examples/*.uasm "$ROOT"/tests/*.uasm; do
        b=$(basename "$f" .uasm)
        if ! "$UASM" compile "$f" -o "$WORK/$b.uo" >/dev/null 2>&1; then
            fail "compile $b"
            continue
        fi
        for fmt in uasm json yaml; do
            "$UASM" dump -f "$fmt" "$WORK/$b.uo" >/dev/null 2>&1 || fail "dump $fmt $b"
        done
    done
}

expect() {
    want=$1
    name=$2
    shift 2
    "$UASM" run "$WORK/$name.uo" -- "$@" >/dev/null 2>&1
    got=$?
    [ "$got" = "$want" ] || fail "run $name: got $got want $want"
}

expect_jit() {
    want=$1
    name=$2
    shift 2
    "$UASM" run "$WORK/$name.uo" -j 4 -- "$@" >/dev/null 2>&1
    got=$?
    [ "$got" = "$want" ] || fail "jit $name: got $got want $want"
}

roundtrip() {
    for f in "$ROOT"/examples/*.uasm "$ROOT"/tests/*.uasm; do
        b=$(basename "$f" .uasm)
        [ -f "$WORK/$b.uo" ] || continue
        "$UASM" dump "$WORK/$b.uo" > "$WORK/$b.rt1" 2>/dev/null || continue
        "$UASM" compile "$WORK/$b.rt1" -o "$WORK/$b.rt.uo" >/dev/null 2>&1 || { fail "reassemble $b"; continue; }
        "$UASM" dump "$WORK/$b.rt.uo" > "$WORK/$b.rt2" 2>/dev/null
        cmp -s "$WORK/$b.rt1" "$WORK/$b.rt2" || fail "round-trip $b"
    done
}

compile_all
expect 42 basics i32:40 i32:2
expect 1  control-flow i32:10 i32:20
expect 6  showcase f64:4.0 f64:2.0
expect 16 v0.4-features
expect 1  syscall-demo
expect 0  isa-core
expect 0  isa-flow
expect_jit 42 basics i32:40 i32:2
expect_jit 1  control-flow i32:10 i32:20
roundtrip

if [ "$FAILS" -eq 0 ]; then
    echo "all checks passed"
    exit 0
fi
echo "$FAILS failure(s)"
exit 1
