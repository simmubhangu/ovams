#!/bin/sh

test_file=$1
inc_dir=`dirname "$test_file"`
log_file=$1.log
log_file_stdout=$1.stdout
log_file_stderr=$1.stderr
valgrind_file=$1.valgrind

rm -f "${valgrind_file}"

printf "TEST: ${test_file} ... "

TEST_CMD="./vams -I testcases/testsuite -I '${inc_dir}' '${test_file}' ./dump-vpi.vpi" 
##TEST_CMD="valgrind --leak-check=full --log-file='${valgrind_file}' ${TEST_CMD}"

sh -c "${TEST_CMD}" > "$log_file_stdout" 2> "$log_file_stderr"
RETVAL=$?

rm -f "${log_file}"

printf "### COMMAND:  ${TEST_CMD}\n" > "${log_file}"
printf "### RETVAL: ${RETVAL}\n" >> "${log_file}"

if [ -s "${log_file_stderr}" ] ; then
	printf "### STDERR OUTPUT BELOW:\n" >> "${log_file}" ;
	cat "${log_file_stderr}" >> "${log_file}" ;
fi

if [ -s "${log_file_stdout}" ] ; then
	printf "### STDOUT OUTPUT BELOW:\n" >> "${log_file}" ;
	cat "${log_file_stdout}" >> "${log_file}" ;
fi

if test "${RETVAL}" != "0" ; then
	printf "failed" ;
else
	printf "success" ;
fi

if [ -s "${valgrind_file}" ]; then
	grep -q "no leaks are possible" "${valgrind_file}"
	LEAKS=$?
	if test "${LEAKS}" != "0" ; then
		printf " [LEAK]" ;
	fi
fi

printf "\n"

if [ -s "${log_file_stderr}" ] ; then
	printf "### STDERR OUTPUT: ############################################################\n" ;
	cat "${log_file_stderr}" ;
	printf "### See '${log_file}' for full log and return code.\n" ;
	printf "###############################################################################\n" ;
fi

if [ -s "${valgrind_file}" ]; then
	if test "${LEAKS}" != "0" ; then
		if test "${RETVAL}" = "0" ; then
			printf "### VALGRIND OUTPUT: ##########################################################\n" ;
			cat "${valgrind_file}" ;
			printf "###############################################################################\n" ;
		fi ;
		# always append valgrind output to log file when leaking:
		printf "### VALGRIND OUTPUT: ##########################################################\n" >> ${log_file};
		cat "${valgrind_file}" >> ${log_file} ;
		printf "###############################################################################\n" >> ${log_file};
	fi ;
fi

rm -f "${log_file_stdout}"
rm -f "${log_file_stderr}"

