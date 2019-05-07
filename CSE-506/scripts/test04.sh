#!/bin/sh
# Test if the backup versions are creaeted as per maxver
# set -x
separator="---------------------------------------------------------------"
echo $separator
echo $'Test if number of backup versions are less than maxver'
echo $separator

myvar=$(cat /proc/mounts | grep bkpfs)
chrlen=${#myvar}
if [ "$chrlen" -gt 0 ]; then
        umount /test/mntpt/
fi
cd /test/lowerdir
rm -rf ..?* .[!.]* *
mount -t bkpfs -o maxver=3 /test/lowerdir /test/mntpt

echo dwight >  /test/mntpt/office.txt
echo dwight >> /test/mntpt/office.txt
echo dwight >> /test/mntpt/office.txt
echo dwight >> /test/mntpt/office.txt
echo dwight >> /test/mntpt/office.txt
echo dwight >> /test/mntpt/office.txt
echo dwight >> /test/mntpt/office.txt

# at this point a backup folder needs to be created.
# check in lowerdir
var=$(ls -1a /test/lowerdir/.office.txt.bkp | wc -l)
echo $var

# 3 backups and 2 for . and ..
if [ "$var" -eq 5 ] ; then
        printf "SUCCESS : Maxver constraint intact!\n"
else
        printf "FAILED : Maxver constraint breach!\n"
fi

cd /test/lowerdir
rm -rf ..?* .[!.]* *
