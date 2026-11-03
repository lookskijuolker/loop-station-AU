#!/usr/bin/env bash
# Static checks that a built .component can load on an Intel Mac running macOS 10.12.
# Usage: scripts/check_macos_compat.sh path/to/Plugin.component [min-version]
set -euo pipefail

BUNDLE="${1:?usage: $0 path/to/Plugin.component [min-version]}"
WANT_MIN="${2:-10.12}"
NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$BUNDLE/Contents/Info.plist")"
BIN="$BUNDLE/Contents/MacOS/$NAME"
fail=0

echo "== Binary: $BIN"
file "$BIN"

echo "== Architectures"
ARCHS="$(lipo -archs "$BIN")"
echo "$ARCHS"
if [[ "$ARCHS" != "x86_64" ]]; then
  echo "ERROR: expected x86_64 only, got: $ARCHS"; fail=1
fi

echo "== Minimum OS load command"
# ld64 writes LC_VERSION_MIN_MACOSX for targets < 10.14, LC_BUILD_VERSION otherwise.
LOADCMDS="$(otool -l "$BIN")"
MINOS="$(echo "$LOADCMDS" | awk '
  /cmd LC_VERSION_MIN_MACOSX/ {m=1; next}
  /cmd LC_BUILD_VERSION/      {b=1; next}
  m && $1=="version" {print $2; exit}
  b && $1=="minos"   {print $2; exit}')"
echo "minos = ${MINOS:-<none>}"
if [[ "$MINOS" != "$WANT_MIN" ]]; then
  echo "ERROR: expected minimum OS $WANT_MIN"; fail=1
fi

echo "== Linked libraries / frameworks"
LIBS="$(otool -L "$BIN" | tail -n +2)"
echo "$LIBS"
# Frameworks a plugin has no business loading. On a real Sierra machine, a linked
# framework pulled in a mismatched private MobileDevice.framework and broke dlopen.
FORBIDDEN_FW='/(WebKit|DiscRecording|MobileDevice|UniformTypeIdentifiers)\.framework/'
if echo "$LIBS" | grep -Eq "$FORBIDDEN_FW"; then
  echo "ERROR: forbidden framework linked:"
  echo "$LIBS" | grep -E "$FORBIDDEN_FW"
  fail=1
fi

echo "== Undefined C++ runtime symbols that do not exist in Sierra's libc++"
# Each of these lives in the libc++/libc++abi dylib only on newer macOS releases.
# If one of them is referenced, the plugin fails to load on 10.12 with a
# "Symbol not found" dyld error (auval reports it as "could not open component").
DENY='St11align_val_t|bad_optional_access|bad_variant_access|bad_any_cast|__fs10filesystem|8to_chars|10from_chars|__itoa|__libcpp_verbose_abort|atomic_wait|atomic_notify|__cxx_contention|3pmr|basic_syncbuf|__libcpp_debug_function'
BAD="$(nm -u "$BIN" | grep -E "$DENY" || true)"
if [[ -n "$BAD" ]]; then
  echo "ERROR: found symbols unavailable on macOS $WANT_MIN:"
  echo "$BAD" | c++filt
  fail=1
else
  echo "none found"
fi

echo "== Info.plist AudioComponents"
/usr/libexec/PlistBuddy -c 'Print :AudioComponents' "$BUNDLE/Contents/Info.plist"

if [[ $fail -ne 0 ]]; then
  echo "Compatibility check FAILED"; exit 1
fi
echo "Compatibility check passed"
