#!/bin/sh
# Test if the backup file is created when a file is
# created and opened in write mode in backup folder.
#
# set -x
separator="---------------------------------------------------------------"
echo $separator
echo $'Test if the backup files are created'
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

var=$(ls -a /test/lowerdir/.office.txt.bkp | grep .office.txt.1)
echo $var

if [ "$var" == ".office.txt.1" ] ; then
        printf "SUCCESS : Backup file created!\n"
else
        printf "FAILED : Backup filer creating failed!\n"
fi

cd /test/lowerdir
rm -rf ..?* .[!.]* *
