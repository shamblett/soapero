#!/bin/bash
set -x
SOAPERO_PATH=../../cmake-build-debug/soapero
SOAPERO_RES_PATH=../../resources/

SERVICE_NAME=OpenLDBWS
#WSDL_URL=https://www.w3schools.com/xml/tempconvert.asmx?WSDL
WSDL_DIR=./wsdl
SERVICE_DIR=./openldbws
BUILD_DIR=./build

# Download WSDL
#rm -rf ${WSDL_DIR}
#mkdir -p ${WSDL_DIR}
#wget "${WSDL_URL}" -O ${WSDL_DIR}/${SERVICE_NAME}.wsdl

# Parse WSDL
mkdir -p ${SERVICE_DIR}
${SOAPERO_PATH} ${WSDL_DIR} ${SERVICE_DIR} --namespace=OpenLDBWS --output-mode=CMakeLists --resources-dir=${SOAPERO_RES_PATH} --service-name=${SERVICE_NAME}

# Build
rm -rf ${BUILD_DIR}
mkdir ${BUILD_DIR}
cd ${BUILD_DIR}
cmake -G "Unix Makefiles" ../ -DCMAKE_VERBOSE_MAKEFILE=ON

make clean
make all
