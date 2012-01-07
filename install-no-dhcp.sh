# Script d'installation pour le projet sur Mini2440
# Ce script permet de compiler l'intégralité du projet (Bootloader, Linux, Application)
# et de fournir les images prêtes pour l'envoie sur la carte.

######## ETAPE 1 - Création des différents dossiers ####

echo "---- Etape 1 - Création des différents dossiers ----"
mkdir Projet-Theou
cd Projet-Theou
mkdir Compilation
mkdir Compilation/linux
mkdir Compilation/crosstool-ng
mkdir Compilation/vboot
mkdir Compilation/busybox
mkdir Compilation/tslib
mkdir Compilation/freetype2
mkdir Compilation/application
mkdir Compilation/elfkicker

cp ../Application/Concours.c Compilation/application

######## ETAPE 2 - Téléchargement des sources ##########

cd ../Archives
echo "---- Etape 2 - Installations des sources ----"


## Chaine de compilation croisée 
echo "Installation de crosstool-ng"

tar xjf crosstool-ng-1.12.3.tar.bz2
mv  crosstool-ng-1.12.3/* ../Projet-Theou/Compilation/crosstool-ng
rm -R crosstool-ng-1.12.3

## Bootloader
echo "Installation de vboot"
unzip vboot_20100106.zip
mv vboot/* ../Projet-Theou/Compilation/vboot
rm  -R vboot/
rm vboot.bin

## Linux
echo "Installation de linux"
tar xzf linux-2.6.32.2-mini2440_20110413.tgz
mv linux-2.6.32.2/* ../Projet-Theou/Compilation/linux
rm -R linux-2.6.32.2

## Application
echo "Installation de busybox"
tar xzf linux-busybox-mini2440-1.13.3.tgz
mv busybox-1.13.3/* ../Projet-Theou/Compilation/busybox
rm -R busybox-1.13.3

echo "Installation de tslib"
tar xzf tslib.tar.gz
mv  tslib/* ../Projet-Theou/Compilation/tslib
rm -R tslib 

echo "Installation de freetype2"
tar xjf freetype-2.4.6.tar.bz2
mv  freetype-2.4.6/* ../Projet-Theou/Compilation/freetype2
rm -R freetype-2.4.6

echo "Installation de super-strip"
tar xzf ELFkickers-3.0.tar.gz
mv ELFkickers-3.0/* ../Projet-Theou/Compilation/elfkicker
rm -R ELFkickers-3.0
######## ETAPE 3 - Installation des configurations ##########

echo "Etape 3 - Installation des configurations"

cd ../Config-no-dhcp
cp config_linux ../Projet-Theou/Compilation/linux/.config
cp config_busybox ../Projet-Theou/Compilation/busybox/.config
cp config_crosstool-ng ../Projet-Theou/Compilation/crosstool-ng/.config
cp config_uclibc ../Projet-Theou/Compilation/crosstool-ng/.config-uclibc

cd ..
######## ETAPE 4 - Compilation des logiciels ##########

echo "Etape 4 - Compilation des logiciels"


## Chaine de compilation croisée

echo "Création de la chaine de compilation croisée"

unset CC
unset CROSS_COMPILE

echo "Compilation de l'outil"
cd Projet-Theou/Compilation/crosstool-ng
ln -s /usr/bin/g++-4.4 /usr/bin/g++
./configure
make -j2
make install

echo "Compilation de la chaine de compilation croisée"
mkdir -p .build/tarballs
cp ../../../Archive-crosstools/* .build/tarballs
./ct-ng build.2

echo "Mise en place de l'environnement de compilation croisée"
DIR="$( cd "$( dirname "$0" )" && pwd )"
PATH=$DIR/x-tools/arm-jbtheou-linux-uclibcgnueabi/bin:$PATH
export PATH
CROSS_COMPILE=arm-jbtheou-linux-uclibcgnueabi-
CC="${CROSS_COMPILE}gcc -march=armv4t -mtune=arm920t"
export CROSS_COMPILE
export CC

## Bootloader

echo "Compilation du bootloader"

## On applique le patch (Configuration (modification du .h), sup des libs,etc)

cd ../vboot
patch -p1 < ../../../Patch/vboot/000-Complet.patch
make

## Création du système de fichier root

echo "Création du système de fichier root"
cd ..
mkdir rootfs
cd rootfs

## Création des dossiers du système de fichier 

mkdir dev
mkdir bin
mkdir -p etc/init.d
mkdir -p mnt/sdcard
mkdir proc
mkdir sbin
mkdir sys
mkdir -m 777 tmp
mkdir usr
mkdir var
mkdir dev/block
mkdir dev/input
mkdir -p usr/local/tslib/etc

## Copie des fichiers de configuration 

cp ../../../Fichier_rootfs/fstab etc/
cp ../../../Fichier_rootfs/inittab etc/
cp ../../../Fichier_rootfs/default.script etc/
cp ../../../Fichier_rootfs/rcS etc/init.d/rcS
cp ../../../Fichier_rootfs/ts.conf usr/local/tslib/etc

## Création des entrées dans dev

cd dev/
mknod -m 622 console c 5  1
mknod -m 622 fb0 c 29 0
mknod -m 666 block/mmcblk0p1 b 179 1
mknod -m 622 input/event0 c 13 64

## Compilation de super-strip

cd ../../elfkicker
make
SSTRIP_DIR=$(pwd)/sstrip
## Compilation de busybox

echo "Compilation de busybox"

cd ..
DIR="$( cd "$( dirname "$0" )" && pwd )"
cd busybox
patch -p1 < ../../../Patch/busybox/000-Complet.patch
make ARCH=arm -j2

## Super strip de busybox
$SSTRIP_DIR/sstrip busybox
make ARCH=arm CONFIG_PREFIX=$DIR/rootfs install 
cd ../rootfs
ln -s sbin/init init

## Compilation de tslib
echo "Compilation de tslib"

cd ../tslib
patch -p2 < ../../../Patch/tslib/000-Complet.patch
./autogen.sh
./configure --host=arm --enable-linear=static --enable-dejitter=static --enable-variance=static --enable-pthres=static --enable-input=static --disable-linear-h2200 --disable-ucb1x00 --disable-corgi --disable-collie --disable-h3600 --disable-mk712 --disable-arctic2 --disable-tatung --disable-dmc --disable-touchkit
make -j2

## Compilation de freetype2
echo "Compilation de freetype2"

cd ../freetype2
patch -p1 < ../../../Patch/freetype2/000-Complet.patch
./configure --host=arm
make -j2

## Compilation de l'application Concours 
echo "Compilation de l'application du Concours"

cd ../application
arm-jbtheou-linux-uclibcgnueabi-gcc -static  -ffunction-sections -fdata-sections -Wl,--gc-sections Concours.c -march=armv4t -mtune=arm920t -I ../freetype2/. -I ../freetype2/include/ -I ../tslib/ -I ../tslib/src -I ../tslib/tests  -L ../freetype2/objs/.libs/ -lfreetype  -L ../tslib/src/.libs/ -lts -Wall -Os -o Concours
$SSTRIP_DIR/sstrip Concours
cp Concours ../rootfs/bin

## Création de l'initramfs

echo "Création de l'initramfs"
cd ..
chmod -R root:root rootfs
cd rootfs
find . | cpio -H newc -o > ../linux/initramfs.cpio

## Compilation du noyau linux

echo "Création du noyau linux"
cd ../linux
patch -p1 < ../../../Patch/linux/000-Complet.patch
make Image -j2

## Copie des fichiers dans le répertoire de destination finale 

cp arch/arm/boot/Image ../../../Image_finale/
cp ../vboot/vboot.bin ../../../Image_finale/


