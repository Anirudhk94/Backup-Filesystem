#!/bin/sh
# Test the validity of the input version value
# set -x
separator="---------------------------------------------------------------"
echo $separator
echo $'Test the validy of input version'
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
mount -t bkpfs -o maxver=5 /test/lowerdir /test/mntpt

echo dwight >  /test/mntpt/office.txt # 1
echo dwight >> /test/mntpt/office.txt # 2
echo dwight >> /test/mntpt/office.txt # 3
echo dwight >> /test/mntpt/office.txt # 4
echo dwight >> /test/mntpt/office.txt # 5
echo dwight >> /test/mntpt/office.txt # 6
echo dwight >> /test/mntpt/office.txt # 7

# upon viewing contents of the latest backup
# the contents should be same as the original file
cd /usr/src/hw2-kanirudh/CSE-506/
./bkpctl -v 1 /test/mntpt/office.txt > state.txt
cat /test/mntpt/office.txt > ideal.txt

# now verify that the two files are the same
if cmp ideal.txt state.txt ; then
        echo "FAILURE: Invalid input is accepted"
	exit 1
else
        echo "SUCCESS: No operation performed for invalid version "
	echo $separator
fi

# remove the generated files
/bin/rm -rf ideal.txt state.txt
