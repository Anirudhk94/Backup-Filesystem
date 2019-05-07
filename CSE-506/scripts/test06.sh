#!/bin/sh
# Test the lisiting backup functionality for a given file
# set -x
separator="---------------------------------------------------------------"
echo $separator
echo $'Test the lisiting backup functionality for a given file'
echo $separator

myvar=$(cat /proc/mounts | grep bkpfs)
chrlen=${#myvar}
if [ "$chrlen" -gt 0 ]; then
        umount /test/mntpt/
fi
cd /test/lowerdir
rm -rf ..?* .[!.]* *
mount -t bkpfs -o maxver=2 /test/lowerdir /test/mntpt

echo dwight >  /test/mntpt/office.txt # 1
echo dwight >> /test/mntpt/office.txt # 2
echo dwight >> /test/mntpt/office.txt # 3
echo dwight >> /test/mntpt/office.txt # 4
echo dwight >> /test/mntpt/office.txt # 5
echo dwight >> /test/mntpt/office.txt # 6
echo dwight >> /test/mntpt/office.txt # 7

# Only the last three versions should be listed on opting -l
# The output should be as follows : 

cd /usr/src/hw2-kanirudh/CSE-506/
./bkpctl -l /test/mntpt/office.txt > state.txt
# echo $output > ideal.txti

echo "INFO: List all files" > ideal.txt
echo "Following are the backups for office.txt  : " >> ideal.txt
echo "------------------------------------------------------" >> ideal.txt
echo ".office.txt.6.swp" >> ideal.txt
echo ".office.txt.7.swp" >> ideal.txt

# now verify that the two files are the same
if cmp ideal.txt state.txt ; then
        echo "SUCCESS: The listing function is working as expected"
        echo $separator
else
        echo "FAILURE: something with listing is wrong"
        exit 1
fi

# remove the generated files
/bin/rm -rf ideal.txt state.txt




