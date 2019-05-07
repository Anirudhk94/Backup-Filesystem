#!/bin/sh
# Test if things work in case of folder hierarchy
#
# set -x
separator="---------------------------------------------------------------"
echo $separator
echo $'Test if things work in case of folder hierarchy'
echo $separator

myvar=$(cat /proc/mounts | grep bkpfs)
chrlen=${#myvar}
if [ "$chrlen" -gt 0 ]; then
        umount /test/mntpt/
fi
cd /test/lowerdir
rm -rf ..?* .[!.]* *
mount -t bkpfs /test/lowerdir /test/mntpt

mkdir /test/mntpt/office/
echo dwight > /test/mntpt/office/office.txt

# at this point a backup file needs to be created.
# check in lowerdir

var=$(ls -a /test/lowerdir/office/ | grep .office.txt.bkp)
echo $var

if [ "$var" == ".office.txt.bkp" ] ; then
        printf "SUCCESS : Backup folder created!\n"
else
        printf "FAILED : Backup folder creating failed!\n"
fi


var=$(ls -a /test/lowerdir/office/.office.txt.bkp | grep .office.txt.1)
echo $var

if [ "$var" == ".office.txt.1" ] ; then
        printf "SUCCESS : Backup file created!\n"
else
        printf "FAILED : Backup filer creating failed!\n"
fi

cd /test/lowerdir
rm -rf ..?* .[!.]* *
