#!/bin/bash
cc="bin/ocean64"

for f in tests/*.c;
do
	filename="${f%.*}"
	$cc $f "$filename"
done

#for f in tests/*.test;
#do
#	./$f
#	echo $?
#done

check_return()
{
	eval "tests/$1"
	retval=$?
	if [ $retval -ne "$2" ]; then
		echo "Fail for $1, expected $2 got $retval"
		exit
	fi
}

check_return exit-code 123
check_return precedence 18
check_return while-loop 9
