#!/bin/sh
# Test if the backup folder is created when a file is
# created and opened in write mode.
#
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

echo dwight > /test/mntpt/office.txt 

# at this point a backup folder needs to be created. 
# check in lowerdir

var=$(ls -a /test/lowerdir/ | grep .office.txt.bkp)
echo $var

if [ "$var" == ".office.txt.bkp" ] ; then
	printf "SUCCESS : Backup folder created!\n"
else
	printf "FAILED : Backup folder creation failed!\n"
fi

cd /test/lowerdir
rm -rf ..?* .[!.]* *
