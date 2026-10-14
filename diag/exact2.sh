#!/bin/bash
# Run one alire-index-checks gh-build-crate.sh variant for the checked-out
# alire-index PR merge ref, starting from a clean Alire state.
# Usage: exact2.sh <upstream|patched> <stable|dev>
set -u
variant=$1
alr_kind=$2
case "$variant" in
   upstream) script=checks-upstream/scripts/gh-build-crate.sh ;;
   patched) script=checks-patched/scripts/gh-build-crate.sh ;;
   *) echo "unknown variant $variant" >&2; exit 2 ;;
esac
# Isolation between consecutive attempts in one runner: gh-build-crate.sh adds
# a "local" index and fails if one already exists, and it reuses /tmp/ac.
alr -n index --del local >/dev/null 2>&1 || true
rm -rf opencv_imgproc_0.2.0_* /tmp/ac
unset SDKROOT DEVELOPER_DIR
log="$RUNNER_TEMP/exact-$alr_kind-$variant.log"
echo "VARIANT=$variant ALR=$alr_kind $(alr --version) script=$script ($(git -C "$(dirname "$script")/.." rev-parse HEAD))"
"$script" > "$log" 2>&1
status=$?
{
   echo "== $alr_kind/$variant image=$ImageVersion exit=$status"
   grep -E '^SDKROOT set to|TEST RESULT|^Total Tests Run|^Successful Tests|^Failed Assertions|^Unexpected Errors|^FAIL |malformed file|unknown architecture|Given name already|CRATE .* TEST ENDED' "$log" | sort | uniq -c
} | tee "$RUNNER_TEMP/exact-$alr_kind-$variant.summary"
tail -n 40 "$log"
exit $status
