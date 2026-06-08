#!/bin/sh
############################################################
#
# Script for copying data from file-based database to db. 
#
# For usage run: files2db.sh -h
# 
# Example (if external ISROOT is used, under cygwin): 
# sh files2db.sh -i d:/ISROOT
#
# $Id: files2db.sh,v 1.1.1.1 2005/01/20 15:19:38 sourcer Exp $
############################################################

source common

#populates database taking key and value from $1 and the rest of the line - accordingly
addRecord(){
        key=$1
        shift
        
        #remove trailing and leading spaces, tabs, white and comment lines:
	trimmedLine=`echo "$@" | awk '{gsub(/^[ \t]+|[ \t]+$/,"");print}' | grep '.' | sed -e '/^#.*$/d'`
	
	if [ -n "$trimmedLine" ]
	then
		echo "value=$trimmedLine"
		./is -k "$key" -v "$trimmedLine"
	else
		echo "blank line - skipped"
	fi
}

find ${INDEX} -type f -print | grep -v CVS | grep -v .svn |
while read file
do
	filename="`basename ${file}`"
	
	echo "Processing ${file}..."
	
	echo "key=${filename}"	
	while read line
	do
		addRecord "$filename" "$line"
	
	done <"${file}"
	
	#pipe problem reading the last line (not called in the above-mentioned loop):
	addRecord "$filename" "$line"
	
	echo ""
done
