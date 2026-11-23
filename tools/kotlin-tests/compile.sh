#!/bin/bash
# usage: kc.sh out.jar src...
L=/opt/gradle-8.14.3/lib
CP=$L/kotlin-compiler-embeddable-2.0.21.jar:$L/kotlin-stdlib-2.0.21.jar:$L/kotlin-script-runtime-2.0.21.jar:$L/kotlin-reflect-2.0.21.jar:$L/trove4j-1.0.20200330.jar:$L/kotlinx-coroutines-core-jvm-1.6.4.jar:$L/annotations-24.0.1.jar:$L/kotlin-daemon-embeddable-2.0.21.jar
OUT=$1; shift
java -cp $CP org.jetbrains.kotlin.cli.jvm.K2JVMCompiler -no-stdlib -no-reflect -cp $L/kotlin-stdlib-2.0.21.jar -d $OUT "$@" 2>&1 | grep -v "^warning" 
