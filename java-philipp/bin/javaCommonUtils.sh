#!/bin/bash

#########################################################################################
# JVM location
#########################################################################################
JAVA_HOME="/home/filipp.andronov/jdk1.7.0"
if [ "$JAVA_HOME" = "" ]; then
    for d in `find /usr/lib/jvm -maxdepth 1 -name "*java*sun*" | sort -r`; do
        if [ -e $d/bin/java ]; then
            JAVA_HOME=$d
            break
        fi
    done

    if [ "$JAVA_HOME" = "" ]; then
            for d in `find /opt -maxdepth 1 -name "jdk*" | sort -r`; do
		if [ -e $d/bin/java ]; then
	            JAVA_HOME=$d
	            break
	        fi
	    done
    fi

    if [ "$JAVA_HOME" = "" ]; then
        echo "Error: No suitable jvm found. Using default one." 1>&2
    fi

    JAVA_VERSION=$("${JAVA_HOME}/bin/java" -version 2>&1 | awk -F '"' '/version/ {print $2}')
    echo "Java version is: $JAVA_VERSION"
fi

#########################################################################################
# Standard java project layout
#########################################################################################
readonly LOCATION=$( cd -P "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
readonly BASE_DIR=$(cd "${LOCATION}/.." && pwd -P)
readonly BIN_DIR=$(cd "${BASE_DIR}/bin" && pwd -P)
readonly LIB_DIR=$(cd "${BASE_DIR}/lib" && pwd -P)
readonly ETC_DIR=$(cd "${BASE_DIR}/etc" && pwd -P)
readonly LOG_DIR=$(cd "${BASE_DIR}/log" && pwd -P)
readonly PATCHES_DIR=$(cd "${BASE_DIR}/patches" && pwd -P)

#########################################################################################
#  Prints message to console and terminates execution
#########################################################################################
#
# arg0 mesg to show
function fatalError() {
	echo $1 1>&2;
	exit -1;
}

#########################################################################################
#  Return common java config suffix derived from the app name
#########################################################################################
#
# App name is a usual artifactId which is written according to CamelCase
# standard. To derive config suffix we replaceing all Uppper letter with
# leading "-" char and conver whole sting to lower case
#
# Example: JavaServiceHome => java-service-home
#
function getConfigSuffixFromAppName() {
    echo "$1" | sed -e's/\([A-Z]\)/-\1/g' | tr "[:upper:]" "[:lower:]" | sed -e's/^-//'
}

#########################################################################################
#  Return classpath file full name drived from the app name
#########################################################################################
#
# Classpath file should has name <artifactId>-<artifactVersion>.classpath and been located
# in ${ETC_DIR}. So we are trying to find all files like that the select latest version
#
# arg0 appName application name
#
function getClassPathFileName() {
   local ANAME=$1;
   # TODO: There is a small issue here - during version comparision SNAPSHOT classpath
   # file wins of final version.
   local VERSION_LIST=( $(find ${ETC_DIR} -iname "${ANAME}-*.classpath" -exec basename '{}' .classpath \; | sort -r) );

   if [ "${#VERSION_LIST[@]}" -lt  1 ]; then
	$(fatalError "Failed to find a classpath file for the application ${ANAME}");
   fi

   echo "$ETC_DIR/${VERSION_LIST[0]}.classpath";
   return 0;
}

#########################################################################################
#  Return classpath for a given application
#########################################################################################
#
#
# arg0 appName applicationName
#
function getClassPath() {
	local ANAME=$1;
	local cpFileName=$(getClassPathFileName "${ANAME}");

	local CLASSPATH="${PATCHES_DIR}:${ETC_DIR}";
	if [ -f "$cpFileName" ]; then
		local version=$(basename $cpFileName .classpath | awk 'BEGIN{FS="-"} { if ($NF == "SNAPSHOT") {print $(NF-1) "-" $NF} else {print $NF} }')
		CLASSPATH="$CLASSPATH:${LIB_DIR}/${ANAME}-$version.jar"
		CLASSPATH=$( echo -n "$CLASSPATH:"; cat $cpFileName | sed "s%\.\.%${BASE_DIR}%g" )
	else
		$(fatalError "Failed to read classpath file '$cpFileName' for the application $ANAME");
	fi

	echo $CLASSPATH
	return 0
}

#########################################################################################
#  Return PID file name
#########################################################################################
#
# arg0 app, application name
#
# return PID file full path
function getPidFile() {
    local ANAME=$1; shift

    echo "${LOG_DIR}/${ANAME}.pid"
    return 0;
}

#########################################################################################
#  Return stdout file name
#########################################################################################
#
# arg0 app, application name
#
# return STDOUT file full path
function getStdoutFile() {
    local ANAME=$1; shift

    echo "${LOG_DIR}/${ANAME}.stdout.log"
    return 0;
}

function getLogbackConfigFile() {
    local ANAME=$1; shift
	local suffix=$(getConfigSuffixFromAppName $ANAME);

 	echo "${ETC_DIR}/logback-$suffix.xml";
 	return 0; 		
}

#########################################################################################
#  Start/Stop generic java application
#########################################################################################
#
# arg0 jvm_args
# arg1 app_name
# arg2 main_class
# arg3 cmd={start/stop/run/check}
# arg* will be passed to JVM
function startGenericJavaApp() {
    if [ "$#" -lt 3 ]; then
        echo "Error: not enough params"
        exit 1
    fi

    local JVM_ARGS=$1; shift
    local APP_NAME=$1; shift
    local MAIN_CLASS=$1; shift
    local ACTION=$1; shift
		
    local PID_FILE="$(getPidFile $APP_NAME)"
    local STDOUT_LOG="$(getStdoutFile $APP_NAME)"
    local LOGBACK_CONFIG="$(getLogbackConfigFile $APP_NAME)"
    
    local PROCESS_NAME="$APP_NAME"
    local JVM_CALL="${JAVA_HOME}/bin/java ${JVM_ARGS} -Dlogback.configurationFile=${LOGBACK_CONFIG} ${MAIN_CLASS} $*"
		
	case ${ACTION} in
	    "start" | "run" )
	        export PROCESS_NAME="${APP_NAME}"
	        export CONTEXT="$PROCESS_NAME"
	        export CLASSPATH=$(getClassPath $APP_NAME);

	        cd "${BIN_DIR}"; 
	        echo "${JVM_CALL}" > "${STDOUT_LOG}";
	         
	        if [ "${ACTION}" == "start" ]; then
  		    ${JVM_CALL} >> "${STDOUT_LOG}" 2>&1 &
	            echo $! > "${PID_FILE}"
	        fi
	
	        if [ "${ACTION}" == "run" ]; then
               	    echo $$ > "${PID_FILE}"
	            exec >> "${STDOUT_LOG}" 2>&1 ${JVM_CALL};
	        fi
	    ;;
	    "stop")
	        local PID=`cat ${PID_FILE}`;
	        if [ -z `echo "${PID}" | grep "^[0-9]\+$"` ]; then
	        	echo "Failed to read pid from: ${PID_FILE}" >&2;
                PID=( `ps ax -o pid,cmd | grep "${ANAME}-$i" | grep -v grep | sed -e's/[ ]*\([0-9]\+\).*/\1/g'` );
                fi

                if [ -z "$PID" ]; then
                echo "Failed to find pid for the process: ${ANAME}" >&2;
	        else
                echo "Going to kill the following list of pids: ${PID[@]}" >&2;
                kill "${PID[@]}";
	        fi
	    ;;
	    "check")
	        local STATUS= -1;
	        local PID=`cat ${PID_FILE}`;
	        if [ -z `echo "${PID}" | grep "^[0-9]\+$"` ]; then
                PID=( `ps ax -o pid,cmd | grep "${ANAME}-$i" | grep -v grep | sed -e's/[ ]*\([0-9]\+\).*/\1/g'` );
        	fi
	
	        if [ ! -z "$PID" ]; then
                STATUS=`kill -0 $PID`;
        	fi
	
		if [ "$PID" -eq 0 ]; then
	            echo "Running";
	        else
	            echo "Stopped";
	        fi
	    ;;
	esac
}
