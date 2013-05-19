#!/bin/sh

# check to see if the correct tools are installed
for X in wc mkisofs
do
	if [ "$(which $X)" = "" ]; then
		echo "makeiso.sh error: $X is not in your path." >&2
		exit 1
	elif [ ! -x $(which $X) ]; then
		echo "makeiso.sh error: $X is not executable." >&2
		exit 1
	fi 
done

#check to see if memtest.bin is present
if [ ! -w memtest.bin ]; then 
	echo "makeiso.sh error: cannot find memtest.bin, did you compile it?" >&2 
	exit 1
fi


# enlarge the size of memtest.bin
SIZE=$(wc -c memtest.bin | awk '{print $1}')
FILL=$((1474560 - $SIZE))
dd if=/dev/zero of=fill.tmp bs=$FILL count=1
cat memtest.bin fill.tmp > memtest.img
rm -f fill.tmp

echo "Generating iso image ..."

mkdir "cd"
mkdir "cd/boot"
mv memtest.img cd/boot
cd cd

# Create the cd.README
echo -e "There is nothing to do here\r\r\nMemtest86+ is located on the bootsector of this CD\r\r\n" > README.TXT
echo -e "Just boot from this CD and Memtest86+ will launch" >> README.TXT

mkisofs -A "MKISOFS 1.1.2" -p "Memtest86+ 4.20" -publisher "Samuel D. <sdemeule@memtest.org>" -b boot/memtest.img -c boot/boot.catalog -V "MT410" -o memtest.iso .
mv memtest.iso ../mt420.iso
cd ..
rm -rf cd

echo "Done! Memtest86+ 4.20 ISO is mt420.iso"
