#!/bin/sh
set -e

printf '%s\n' \
    'WITH_PVA=NO' \
    'WITH_QSRV=NO' \
    'XML2_EXTERNAL=YES' \
    'XML2_INCLUDE=/usr/include/libxml2' \
    > configure/CONFIG_SITE.local
