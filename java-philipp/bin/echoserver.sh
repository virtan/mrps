#!/bin/bash

readonly SELF=$( cd -P "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
. ${SELF}/javaCommonUtils.sh

JVM_OPTS="${JVM_OPTS} -Xmx256m -Xms256m"
JVM_OPTS="${JVM_OPTS} -verbose:gc -Xloggc:../log/echoserver.gc.log -XX:+PrintGCDetails"

if [[ "$JAVA_VERSION" > "1.6" ]]; then
	JVM_OPTS="${JVM_OPTS} -XX:+UseG1GC"
elif [[ "$JAVA_VERSION" > "1.4" ]]; then
	JVM_OPTS="${JVM_OPTS} -XX:+UseCompressedOops"
	JVM_OPTS="${JVM_OPTS} -XX:+UseConcMarkSweepGC -XX:+CMSParallelRemarkEnabled -XX:+UseParNewGC"
fi

COMMAND=$1; shift;
startGenericJavaApp "${JVM_OPTS}" "echoserver" "vn.itim.samples.echoserver.ServerRunner" "$COMMAND" "$*"
