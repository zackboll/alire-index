#!/bin/bash
# Disposable diagnostic: GNAT native linking against the macOS SDK selections
# available on a GitHub-hosted runner. No OpenCV or Imgproc involved.
# Usage: minimal.sh <gnat-bin-dir> <gprbuild-bin-dir> <results-file>
set -u
GNAT_BIN=$1
GPR_BIN=$2
RESULTS=$3
HERE=$(cd "$(dirname "$0")" && pwd)
export PATH="$GPR_BIN:$GNAT_BIN:$PATH"
: > "$RESULTS"

section() { echo; echo "===== $* ====="; }
tbd_head() {
   local f=$1/usr/lib/libSystem.tbd
   if [ -f "$f" ]; then
      echo "--- $f (arm64e.x1 mentions: $(grep -c 'arm64e.x1' "$f"))"
      sed -n '1,8p' "$f"
   else
      echo "--- $f missing"
   fi
}

section ENVIRONMENT
echo "ImageOS=${ImageOS:-} ImageVersion=${ImageVersion:-}"
sw_vers
uname -a
uname -m
sysctl -n machdep.cpu.brand_string 2>/dev/null || true
for v in DEVELOPER_DIR SDKROOT LIBRARY_PATH COMPILER_PATH MACOSX_DEPLOYMENT_TARGET CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH; do
   echo "$v=${!v-<unset>}"
done
echo "PATH=$PATH"

section XCODE AND COMMAND LINE TOOLS
echo "xcode-select -p: $(xcode-select -p)"
echo "xcrun --find clang++: $(xcrun --find clang++)"
echo "xcrun --find ld: $(xcrun --find ld)"
echo "xcrun --sdk macosx --show-sdk-path: $(xcrun --sdk macosx --show-sdk-path)"
echo "xcrun --sdk macosx --show-sdk-version: $(xcrun --sdk macosx --show-sdk-version)"
pkgutil --pkg-info=com.apple.pkg.CLTools_Executables 2>&1 | sed 's/^/CLT pkg: /'
echo "CLT SDKs:"; ls -la /Library/Developer/CommandLineTools/SDKs 2>&1
echo "Xcode SDKs:"; ls -la "$(xcode-select -p)/Platforms/MacOSX.platform/Developer/SDKs" 2>&1
for ld in /usr/bin/ld "$(xcrun --find ld)" /Library/Developer/CommandLineTools/usr/bin/ld; do
   echo "--- ld -v: $ld"
   "$ld" -v 2>&1 | head -3
done
echo "--- xcrun clang++ --version"; xcrun clang++ --version
echo "--- CLT clang++ --version"; /Library/Developer/CommandLineTools/usr/bin/clang++ --version 2>&1 | head -2
tbd_head /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
tbd_head "$(xcrun --sdk macosx --show-sdk-path)"

section GNAT TOOLCHAIN
which gnat gcc g++ gprbuild
gnat --version | head -1
g++ -v 2>&1 | tail -3
echo "g++ -print-sysroot: '$(g++ -print-sysroot)'"
echo "g++ -print-prog-name=ld: $(g++ -print-prog-name=ld)"
echo "g++ -print-prog-name=collect2: $(g++ -print-prog-name=collect2)"
gprbuild --version | head -1
gnatls -v 2>&1 | sed -n '/Object Search Path/,/Project Search Path/p' | head -6

# run_case <label> <project-dir> <main> <expect> [extra -largs ...]
run_case() {
   local label=$1 dir=$2 main=$3 expect=$4
   shift 4
   local work log status
   work=$(mktemp -d)
   cp "$HERE/$dir"/* "$work"/
   log="$work/build.log"
   (
      cd "$work"
      gprbuild -q -p -P "$dir.gpr" -largs -v -Wl,-v "$@" > "$log" 2>&1
   )
   status=$?
   echo "--- [$label/$dir] gprbuild exit $status"
   grep -E 'collect2|/ld |-syslibroot|PROGRAM:ld|BUILD|malformed|unknown architecture|Undefined symbols|symbol\(s\) not found' "$log" | cut -c1-600 | head -12
   local run="not-run"
   if [ $status -eq 0 ]; then
      if out=$("$work/bin/$main" 2>&1) && grep -q "$expect" <<< "$out"; then
         run="PASS"
      else
         run="RUN-FAIL"
      fi
      echo "    run: $out"
   fi
   local first
   first=$(grep -m1 -E 'malformed file|unknown architecture|Undefined symbols' "$log" | cut -c1-160)
   printf '%-28s %-6s link=%-3s run=%-8s %s\n' "$label" "$dir" "$status" "$run" "$first" >> "$RESULTS"
   rm -rf "$work"
}

xcode_dev=$(xcode-select -p)
xcode_sdk=$(xcrun --sdk macosx --show-sdk-path)

for dir in hello mixed; do
   main=$dir
   expect=$([ $dir = hello ] && echo 'ADA HELLO OK' || echo 'ADA+C++ MIXED OK')
   section "CASES: $dir"
   run_case "A.default" $dir $main "$expect"
   SDKROOT=$xcode_sdk run_case "B.SDKROOT=xcrun" $dir $main "$expect"
   DEVELOPER_DIR=$xcode_dev run_case "C.DEVELOPER_DIR=Xcode" $dir $main "$expect"
   run_case "D.-isysroot(xcrun)" $dir $main "$expect" -isysroot "$xcode_sdk"
   DEVELOPER_DIR=/Library/Developer/CommandLineTools run_case "E.DEVELOPER_DIR=CLT" $dir $main "$expect"
   for sdk in /Library/Developer/CommandLineTools/SDKs/*.sdk \
              "$xcode_dev"/Platforms/MacOSX.platform/Developer/SDKs/*.sdk; do
      [ -d "$sdk" ] || continue
      case $sdk in
         /Library/Developer/CommandLineTools/*) owner=CLT ;;
         *) owner=Xcode ;;
      esac
      SDKROOT=$sdk run_case "F.SDKROOT=$owner/$(basename "$sdk")" $dir $main "$expect"
   done
done

section RESULTS
cat "$RESULTS"
