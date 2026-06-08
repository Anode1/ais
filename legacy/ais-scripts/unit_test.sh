#!/bin/bash
#sorry - this is bash script: please fix it to be bourne

export INDEX=tmp

#redefine files number threshold - to use smaller number of files
export MAX_FILES_IN_DIR=3

source common

echo "Regression test starting..."

#use new temp directory:
echo "Creating tmp directory"
mkdir tmp


echo "Creating approximately 3500 keys"
i=10
while [ $i -gt 0 ] ;do
	for j in `cat testwords` ;do
		./put -k "$j" -v "/mnt/archive/20041223 #$i"
	done
	i=`expr $i - 1`
	echo $i
done
echo
echo "Creating 62000 keys"
for i in `cat dict` ;do
	./put -k "$i" -v "/this/is/test #$i"
done
echo $i
echo

#find stored before key:
#get  
