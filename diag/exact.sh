#!/bin/bash
# Run the unmodified upstream alire-index-checks gh-build-crate.sh for the
# checked-out alire-index PR merge ref under one Apple environment selection.
# Usage: exact.sh <none|sdkroot|clt> <stable|dev>
set -u
remedy=$1
alr_kind=$2
# Each attempt starts from a clean deployment and Alire cache.
rm -rf opencv_imgproc_0.2.0_* /tmp/ac
case "$remedy" in
   none) ;;
   sdkroot) export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)" ;;
   clt) export DEVELOPER_DIR=/Library/Developer/CommandLineTools ;;
   *) echo "unknown remedy $remedy" >&2; exit 2 ;;
esac
log="$RUNNER_TEMP/exact-$alr_kind-$remedy.log"
echo "REMEDY=$remedy ALR=$alr_kind $(alr --version) SDKROOT=${SDKROOT-<unset>} DEVELOPER_DIR=${DEVELOPER_DIR-<unset>}"
echo "xcrun ld: $(xcrun --find ld)  sdk: $(xcrun --sdk macosx --show-sdk-path)"
alire-index-checks/scripts/gh-build-crate.sh > "$log" 2>&1
status=$?
{
   echo "== $alr_kind/$remedy image=$ImageVersion exit=$status"
   grep -E 'TEST RESULT|^Total Tests Run|^Successful Tests|^Failed Assertions|^Unexpected Errors|^FAIL |malformed file|unknown architecture|CRATE .* TEST ENDED' "$log" | sort | uniq -c
} | tee "$RUNNER_TEMP/exact-$alr_kind-$remedy.summary"
tail -n 60 "$log"
exit $status
