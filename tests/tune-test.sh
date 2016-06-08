#!/bin/bash

source test-common.sh

for mode in tune-all-frames auto-tune
do
  $SMENC -f 420 --no-attack --no-lpc -O1 saw440.wav tune-test.sm
  $SMTOOL tune-test.sm $mode > /dev/null
  if [ "x$($SMTOOL tune-test.sm frame-params 30 | head -5 | awk '
    BEGIN {
      result="OK"
    }
    {
      delta = (NR * 420 - $1) / NR
      if (delta < 0) { delta = -delta } # abs value
      if (delta > 0.1) {
        result = "FAIL"
      }
    }
    END {
      print result
    }')" != "xOK" ]
  then
    echo "tune-test failed, mode $mode"
    $SMTOOL tune-test.sm frame-params 30 | head -5
    exit 1
  fi
done
rm tune-test.sm
exit 0
