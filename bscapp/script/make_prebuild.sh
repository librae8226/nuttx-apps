#!/bin/bash

if [ ! -f "$1" ] || [ ! -d "$2" ]; then
	echo $0 'src_bin dst_dir [release_type]'
	exit 1
fi

src_bin=$1
dst_dir=$2
release_type=$3

if [ hit$release_type = 'hitrelease' ]; then
	release_type='release'
elif [ hit$release_type = 'hittest' ]; then
	release_type='test'
else
	release_type='dev'
fi

cp -v $src_bin $dst_dir/nuttx_bscapp_g`git show --pretty="%h" | head -n 1`_`date +%Y%m%d`.`echo $release_type`.bin
