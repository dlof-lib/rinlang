#!/bin/bash
cd /home/claude/rinlang-main
C=app/src/main/cpp
for s in $(cat build/srcs.txt); do
  o=build/$(echo $s | tr / _).o
  [ -f $o ] && continue
  g++ -std=c++23 -O1 -w -I$C -Icli/rin_vm/include -c $C/$s.cpp -o $o 2>build/$(echo $s | tr / _).err || echo "FAIL $s" >> build/fails.txt
  echo "done $s" >> build/progress.txt
done
echo ALLDONE >> build/progress.txt
