#!/bin/bash

result=0

export GIT_DIR=./does-not-exist
export TIGRC_SYSTEM=
export TIGRC_USER=

src/tig 2>&1 | grep -q "tig: Error in built-in config"
if [ $? == 0 ]
then
	echo "not ok - Errors reported in built-in config"
	result=$(($result+1))
else
	echo "ok - Built-in config loaded"
fi

exit $result
