#!/bin/sh
# Test the visibility scheme for the backup file
# set -x
separator="---------------------------------------------------------------"
echo $separator
echo $'Test the visibility scheme for the backup file'
echo $separator

myvar=$(cat /proc/mounts | grep bkpfs)
chrlen=${#myvar}
if [ "$chrlen" -gt 0 ]; then
        umount /test/mntpt/
fi
cd /test/lowerdir
rm -rf ..?* .[!.]* *
mount -t bkpfs /test/lowerdir /test/mntpt

echo dwight > /test/mntpt/office.txt

# at this point a backup file needs to be created.
# check in lowerdir

var=$(ls -a /test/lowerdir/ | grep .office.txt.bkp)

if [ "$var" == ".office.txt.bkp" ] ; then
        printf "SUCCESS : Backup folder created in lowerdir!\n"
else
        printf "FAILED : Backup folder creating failed!\n"
fi

var=$(ls -a /test/mntpt/ | grep .office.txt.bkp)

if [ "$var" == ".office.txt.bkp" ] ; then
        printf "FAILED : Backup folder visible in mntpt!\n"
else
        printf "SUCCESS : Backup folder hidden in mntpt!\n"
fi



