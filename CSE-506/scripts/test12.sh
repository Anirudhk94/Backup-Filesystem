#!/bin/sh
# Test if the backup file is created for large files
# set -x
separator="---------------------------------------------------------------"
echo $separator
echo $'Test if the backup folder is created when a file is created and opened in write mode.'
echo $separator

myvar=$(cat /proc/mounts | grep bkpfs)
chrlen=${#myvar}
if [ "$chrlen" -gt 0 ]; then
        umount /test/mntpt/
fi
cd /test/lowerdir
rm -rf ..?* .[!.]* *
cd /usr/src/hw2-kanirudh/fs/bkpfs/
rmmod bkpfs.ko
insmod bkpfs.ko
mount -t bkpfs /test/lowerdir /test/mntpt

echo `perl -e 'print "a"x105000'`  > /test/mntpt/office.txt

# at this point a backup folder needs to be created.
# check in lowerdir

var=$(ls -a /test/lowerdir/.office.txt.bkp | grep .office.txt.1)
echo $var

if [ "$var" == ".office.txt.1" ] ; then
        printf "SUCCESS : Large backup file created!\n"
else
        printf "FAILED : Backup filer creation failed!\n"
fi

cd /test/lowerdir
rm -rf ..?* .[!.]* *
