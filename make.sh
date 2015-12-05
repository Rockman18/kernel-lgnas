#!/bin/bash 

#set -x 

IMAGE_DIR="$(pwd)/$(dirname $0)/image"

VERSION=$(cat Makefile |grep ^VERSION\ = |sed 's/.*= //')
PATCHLEVEL=$(cat Makefile |grep ^PATCHLEVEL\ = |sed 's/.*= //')
SUBLEVEL=$(cat Makefile |grep ^SUBLEVEL\ = |sed 's/.*= //')
EXTRAVERSION=$(cat Makefile |grep ^EXTRAVERSION\ = |sed 's/.*= //')
KERNEL_VERSION="${VERSION}.${PATCHLEVEL}.${SUBLEVEL}${EXTRAVERSION}"
KERNEL_IMAGE_NAME=uImage
CURCONFIG=".config"

get_tempfile() {
  TEMP=$(mktemp)
  TEMPFILES=("${TEMPFILES[@]}" "$TEMP")
  echo $TEMP
}

cleanup() {
   if [[ "${tempfiles[*]}" != "" ]]; then
      rm -f "${tempfiles[@]}"
   fi
   exit
}

make_prepare(){
  rm $IMAGE_DIR/* -rf
	mkdir -p $IMAGE_DIR > /dev/null 2>&1
}

prepare() {
  FRONTEND=$(which dialog)
  if [ -z "$FRONTEND" ]; then
    FRONTEND=$BASE_SCRIPT_DIR/bin/dialog
    chmod +x $FRONTEND
    [ "$?" != "0" ] && exit 0
  fi
  make_prepare
}

get_kernel_revision(){
  CUR_REV=$(cat $CURCONFIG | grep KERNEL_REVISION | sed 's/.*=//')
  DEF_REV=$(cat $DEFCONFIG | grep KERNEL_REVISION | sed 's/.*=//')
}


parse_args() {
  ARGS_ALL="$@"

  while [ "$1" != "" ]; do
    case "$1" in
      "cross-compile")		MAKE_CROSS=yes ;;
      "clean")		MAKE_CLEAN=yes ;;
      "module")	  MAKE_MODULE=yes ;;
      "install")	MAKE_INSTALL=yes ;;
      "header")	  MAKE_HEADER=yes ;;
      *)		echo "Unknown options : $1"; exit 1 ;;
    esac
    shift
  done


  # STEP 3: make firmware

  MAKE="make"
  [ "$MAKE_CROSS"   = "yes" ] && {
		MAKE="make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE"
  } 
}

parse_args_model() {
  ARGS_ALL="$@"

  while [ "$1" != "" ]; do
    case "$1" in
      "netenc")			 
									NETENC="netenc_"
									;;
      "ns1")			 
									ARCH=arm
									CROSS_COMPILE=arm-none-linux-gnueabi-
									DEFCONFIG="./arch/arm/configs/ns1_${NETENC}defconfig"
									;;
      "nc1")			 
									ARCH=arm
									CROSS_COMPILE=arm-none-linux-gnueabi-
									DEFCONFIG="./arch/arm/configs/nc1_${NETENC}defconfig"
									;;
      "nc5")			 
									ARCH=powerpc
									CROSS_COMPILE=ppc_4xxFP-
									DEFCONFIG="./arch/$ARCH/configs/nc5_${NETENC}defconfig"
									;;
      "nc2")			 
									ARCH=arm
									CROSS_COMPILE=arm-none-linux-gnueabi-
									DEFCONFIG="./arch/arm/configs/nc2_${NETENC}defconfig"
									;;
      "nc2-1")			 
									ARCH=arm
									CROSS_COMPILE=arm-none-linux-gnueabi-
									DEFCONFIG="./arch/arm/configs/nc21_${NETENC}defconfig"
									;;
      "nc3")			 
									ARCH=powerpc
									CROSS_COMPILE=ppc_4xxFP-
									DEFCONFIG="./arch/$ARCH/configs/nc3_${NETENC}defconfig"
									;;
      "nt1")			 
									ARCH=arm
									CROSS_COMPILE=arm-none-linux-gnueabi-
									DEFCONFIG="./arch/arm/configs/nt1_${NETENC}defconfig"
									;;
      "nt1-1")			 
									ARCH=arm
									CROSS_COMPILE=arm-none-linux-gnueabi-
									DEFCONFIG="./arch/arm/configs/nt11_${NETENC}defconfig"
									;;
      "nt3")			 
									ARCH=powerpc
									CROSS_COMPILE=ppc_4xxFP-
									DEFCONFIG="./arch/$ARCH/configs/nt3_${NETENC}defconfig"
									;;
      "ns2")			
									ARCH=i386
									KERNEL_IMAGE_NAME="bzImage"
									CROSS_COMPILE=
                                    DEFCONFIG="./arch/x86/configs/ns2_${NETENC}defconfig"
									;;
      "mm1")			
									ARCH=mips
									CROSS_COMPILE=mips64-unknown-linux-gnu-
									DEFCONFIG="./arch/mips/configs/mm1_${NETENC}defconfig"
									;;

      *)		echo "Unknown options : $1"; exit 1 ;;
    esac
		LGNAS_MODEL=$1
    shift
  done
}

check_cross_compiler(){
  if [ "$MAKE_CROSS"  != "yes" ]; then
	  SYS_ARCH=$(uname -m)
		echo  $(uname -m) |grep $ARCH || {
  	  echo " Your must cross-compile!!!"
  		exit 0
		}
	else
  	which ${CROSS_COMPILE}gcc || {
  	  echo " Your system has not cross-compiler!!!"
  		exit 0
  	}
	fi
}

make_clean(){
	echo "-----------------------------------"
	echo "make clean"
	echo "-----------------------------------"
	echo MAKE=[$MAKE]
	$MAKE clean && \
	$MAKE mrproper && \
	$MAKE $(basename $DEFCONFIG)
  if [ $? != 0 ]; then
	  echo "Kernel config Error!!!"
	  exit 1
  fi

	get_kernel_revision
  REV=$DEF_REV
}
make_do(){
	echo "-----------------------------------"
	echo " make"
	echo "-----------------------------------"
	check_cross_compiler
	$MAKE $KERNEL_IMAGE_NAME  -j4 
  if [ $? != 0 ]; then
	  echo "Kernel compile Error issued!!!"
	  exit 1
  fi
	$MAKE modules -j4 
  if [ $? != 0 ]; then
	  echo "Kernel compile Error issued!!!"
	  exit 1
  fi
}
make_install(){
	echo "-----------------------------------"
	echo " make install"
	echo "-----------------------------------"
	[ "$KERNEL_IMAGE_NAME" != "" ] && { 
    cp ./arch/${ARCH}/boot/${KERNEL_IMAGE_NAME} ${IMAGE_DIR}/${KERNEL_IMAGE_NAME}_${LGNAS_MODEL}_${NETENC}${REV} 
    cp ./System.map  ${IMAGE_DIR}/System_${LGNAS_MODEL}_${NETENC}${REV}.map
    cp ./.config ${IMAGE_DIR}/config_${LGNAS_MODEL}_${NETENC}${REV}
  }
#	$MAKE install INSTALL_PATH=$IMAGE_DIR
#  if [ $? != 0 ]; then
#    echo "Kernel install Error!!!"
#    exit 1
#  fi
	IMAGE_FILES=$(echo $IMAGE_DIR/*-lgnas*)
	for file in $IMAGE_FILES; do 
	  [[ $file = *netenc* ]] && continue 
		[ -f $file ] && mv $file ${file}_${LGNAS_MODEL}_${NETENC}${REV}
	done
  return 0
}
make_module_install(){
	echo "-----------------------------------"
	echo " make modules_install"
	echo "-----------------------------------"
	rm $IMAGE_DIR/lib -rf
	$MAKE modules_install INSTALL_MOD_PATH=$IMAGE_DIR
	tar -cjf $IMAGE_DIR/module_${LGNAS_MODEL}_${NETENC}${REV}.tar.bz2 -C $IMAGE_DIR/ lib --owner=root --group=root
}

make_header_install(){
	echo "-----------------------------------"
	echo " make headers_install"
	echo "-----------------------------------"
	rm $IMAGE_DIR/include -rf
	$MAKE headers_install INSTALL_HDR_PATH=$IMAGE_DIR
	tar -cjf $IMAGE_DIR/header_${LGNAS_MODEL}_${REV}.tar.bz2 -C $IMAGE_DIR/ include --owner=root --group=root
}

select_model_dialog(){
  SELECT_FILE=$(get_tempfile)
	echo "SELECT_FILE=$SELECT_FILE"
	echo "FRONTEND=$FRONTEND"
  $FRONTEND --colors \
	--backtitle "LG NAS information" \
  --radiolist "Choose Model :" 15 40 8 \
  "ns1"   "N4B1 N4D1" off \
  "nc1"   "N2B1 N2D1" off \
  "nc5"   "N2B5 N2D5" on \
  "nc2"   "N2T2"      off \
  "nc2-1" "N2A2"      off \
  "nc3"		"N2T3"			off	\
  "ns2"   "N4B2 N4D2" off \
  "nt1"   "N1T1"      off \
  "nt1-1" "N1A1"      off \
  "nt3"   "N1T3"      off \
  "mm1"   "S1T1"        off \
  2> $SELECT_FILE

  RESULT=$?
  echo 

  # STEP 2: parse arguments
  if [ "$RESULT" = 0 ]; then
    parse_args_model $(cat $SELECT_FILE | sed 's/"//g')
  elif [ "$RESULT" = 1 ]; then
    exit 1
  fi
  echo
}

make_dialog(){
	get_kernel_revision
  # STEP 1: make dialog
  SELECT_FILE=$(get_tempfile)
  $FRONTEND --colors \
	--checklist "Kernel(\Z1$KERNEL_VERSION rev:$CUR_REV\Z0) Compile Dialog(Model:\Z1$LGNAS_MODEL\Z0)" 13 90 16 \
	"cross-compile"  "Cross compile(gcc:\Z1${CROSS_COMPILE}gcc\Z0)" 	"on" \
	"clean" 	       "Clean and defconfig(\Z1$(basename $DEFCONFIG) rev:$DEF_REV\Z0)" 	"off" \
	"install" 	     "Install Kernel" 	  "on" \
	"module" 	       "Install Modules"		"on" \
	"header" 		     "Install Header"		"off" \
	2> $SELECT_FILE

  RESULT=$?
  echo

  # STEP 2: parse arguments
  if [ "$RESULT" = 0 ]; then
    parse_args $(cat $SELECT_FILE | sed 's/"//g')
  elif [ "$RESULT" = 1 ]; then
    exit 1
  fi
  echo

  REV=$CUR_REV	
  [ "$MAKE_CLEAN"   = "yes" ] && {
	  make_clean
	}
  [ "$MAKE_CLEAN"   = "yes" -o \
    "$MAKE_INSTALL" = "yes" -o \
    "$MAKE_MODULE"  = "yes" ] && make_do

  [ "$MAKE_INSTALL" = "yes" ] && make_install
  [ "$MAKE_MODULE"  = "yes" ] && make_module_install
  [ "$MAKE_HEADER"  = "yes" ] && make_header_install
}

main(){

  [ $1 = "all" ] && {
	  make_all $@
	} || {
	  [ "$1" = "netenc" ] && parse_args_model netenc
	  select_model_dialog
    make_dialog
	}
}
# model, netenc
make_model(){
	parse_args_model $1
	parse_args cross-compile clean module install
	MAKE="$MAKE -s"
	make_clean && \
	make_do && \
	make_install && \
	make_module_install
}
make_all() {
  # to be added ns2 mm1 nc5 nt3 
  for model in "nt1" "nt1-1" "nc2" "nc2-1" "nt3" "nc5" "nc3" "ns2"
	do
	  echo "======================"
	  echo "Model : $model"
	  echo "======================"
	  # for network enclosure image
		echo $@ |grep netenc && \
	  [ "$model" != "ns2" ] && parse_args_model netenc
	  make_model $model && {
	    echo "================================"
	    echo "Success : $model"
	    echo "================================"
		} || {
	    echo "================================"
	    echo "Fail : $model  "
	    echo "================================"
		  exit 1
		}
	done
}

trap cleanup INT TERM EXIT
echo "$0 [all:auto model build][netenc:ramdisk image for usb/net enclosure]"
prepare
main $@

