#!/bin/sh
# spark_prove.sh -- prove the SPARK merge unit is free of runtime errors.
# Needs gnatprove on PATH (the SPARK toolset: install via Alire, or the
# GNAT-FSF-builds "gnatprove-*-x86_64-linux" release). See LANG_COMPARISON.md.
set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
command -v gnatprove >/dev/null 2>&1 || {
  echo "gnatprove not on PATH -- install the SPARK toolset (Alire, or a"
  echo "GNAT-FSF-builds gnatprove release), then re-run."; exit 2; }
gnatprove -P "$here/spark/merge.gpr" --level=2 --report=all
