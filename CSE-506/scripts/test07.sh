#!/bin/sh
# Test the delete version functionality 
# set -x
separator="---------------------------------------------------------------"
echo $separator
echo $'Test the delete version functionality'
echo $separator

myvar=$(cat /proc/mounts | grep bkpfs)
chrlen=${#myvar}
if [ "$chrlen" -gt 0 ]; then
        umount /test/mntpt/
fi
cd /test/lowerdir
rm -rf ..?* .[!.]* *
mount -t bkpfs -o maxver=5 /test/lowerdir /test/mntpt

echo dwight >  /test/mntpt/office.txt # 1
echo dwight >> /test/mntpt/office.txt # 2
echo dwight >> /test/mntpt/office.txt # 3
echo dwight >> /test/mntpt/office.txt # 4
echo dwight >> /test/mntpt/office.txt # 5
echo dwight >> /test/mntpt/office.txt # 6
echo dwight >> /test/mntpt/office.txt # 7

# deleting oldest file should result in final versions as,
# 3,4,5,6,7
cd /usr/src/hw2-kanirudh/CSE-506/
./bkpctl -d oldest /test/mntpt/office.txt 
./bkpctl -l /test/mntpt/office.txt > state.txt

echo "INFO: List all files" > ideal.txt
echo "Following are the backups for office.txt  : " >> ideal.txt
echo "------------------------------------------------------" >> ideal.txt
echo ".office.txt.4.swp" >> ideal.txt
echo ".office.txt.5.swp" >> ideal.txt
echo ".office.txt.6.swp" >> ideal.txt
echo ".office.txt.7.swp" >> ideal.txt

# now verify that the two files are the same
if cmp ideal.txt state.txt ; then
        echo "SUCCESS: -d oldest deletion function is working as expected"
        echo $separator
else
        echo "FAILURE: something with -d oldest"
        exit 1
fi

# remove the generated files
/bin/rm -rf ideal.txt state.txt

./bkpctl -d newest /test/mntpt/office.txt
./bkpctl -l /test/mntpt/office.txt > state.txt

echo "INFO: List all files" > ideal.txt
echo "Following are the backups for office.txt  : " >> ideal.txt
echo "------------------------------------------------------" >> ideal.txt
echo ".office.txt.4.swp" >> ideal.txt
echo ".office.txt.5.swp" >> ideal.txt
echo ".office.txt.6.swp" >> ideal.txt

# now verify that the two files are the same
if cmp ideal.txt state.txt ; then
        echo "SUCCESS: -d newest deletion function is working as expected"
        echo $separator
else
        echo "FAILURE: something with -d newest"
        exit 1
fi

# remove the generated files
/bin/rm -rf ideal.txt state.txt

./bkpctl -d all /test/mntpt/office.txt
./bkpctl -l /test/mntpt/office.txt > state.txt
echo "INFO: List all files" > ideal.txt
echo "Following are the backups for office.txt  : " >> ideal.txt
echo "------------------------------------------------------" >> ideal.txt

# now verify that the two files are the same
if cmp ideal.txt state.txt ; then
        echo "SUCCESS: -d all deletion function is working as expected"
        echo $separator
else
        echo "FAILURE: something with -d all"
        exit 1
fi

# remove the generated files
/bin/rm -rf ideal.txt state.txt
