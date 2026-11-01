#!/bin/bash
cd /home/claude/rinlang-main
C=app/src/main/cpp; L=cli/linux/src
for s in main terminal/rin_terminal pkg/semver pkg/toml_lite pkg/sha256 pkg/json_lite pkg/manifest pkg/lockfile pkg/archive pkg/cache pkg/registry pkg/resolver pkg/package_ops pkg/auth pkg/cli_pkg; do
  o=build/cli_$(echo $s | tr / _).o
  [ -f $o ] && continue
  g++ -std=c++23 -O1 -w -I$L -I$C -Icli/rin_vm/include -c $L/$s.cpp -o $o 2>build/cli_$(echo $s | tr / _).err || echo "FAIL $s" >> build/cli_fails.txt
  echo "done $s" >> build/cli_progress.txt
done
echo ALLDONE >> build/cli_progress.txt
