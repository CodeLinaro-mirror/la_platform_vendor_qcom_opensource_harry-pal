#!/bin/sh
####################################################################
# Important note:
# Any change within QC projects may disalign the prebuilts used
# by disco to have apex available dependencies
#
#
####################################################################
/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

HARRY_DIR=$1
VENDOR_DIR="${HARRY_DIR}../../../../../out/target/product/gen4_gvm_cdcsdv/vendor/lib64"
echo "HARRY_DIR = ${HARRY_DIR}"
echo "VENDOR_DIR = ${VENDOR_DIR}"

mkdir -p lib64

cp -rf ${VENDOR_DIR}/libhar-pal.so                      ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libar-gsl.so                       ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/liblx-osal.so                      ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libdiag.so                         ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/vendor.qti.hardware.agm-V1-ndk.so  ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libagmclient.so                    ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libpdmapper.so                     ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libpdnotifier.so                   ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libar-acdb.so                      ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/liblx-ar_util.so                   ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libar-gpr.so                       ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libqti-tinycompress.so             ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libqmi_cci.so                      ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libqmi_common_so.so                ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libqmi_encdec.so                   ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libjson.so                         ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libagm_pcm_plugin.so               ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libagm_mixer_plugin.so             ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libsndcardparser.so                ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libaudio_log_utils.so              ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/lib_oem_plugin_controls.so         ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/lib_default_plugin_controls.so     ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libar-gsl_fe.so                    ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libvui_intf.so                     ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libarmemlog.so                     ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libdev_speaker.so                  ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libstream_pcm.so                   ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libsession_pcm.so                  ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libagmsocket.so                    ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libagmsocket_client.so             ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libsession_ar.so                   ${HARRY_DIR}lib64/
cp -rf ${VENDOR_DIR}/libtinycompress.so                 ${HARRY_DIR}lib64/

#End