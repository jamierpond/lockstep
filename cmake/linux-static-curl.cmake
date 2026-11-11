# eacp-network downloads through libcurl on Linux. lockstep builds a libcurl for
# HTTP(S), WebSocket and file:// only, against mbedTLS, and links both statically,
# so the binary needs no libcurl, TLS or compression library on the machine.
# eacp's find_package(CURL) finds this package through lockstep's redirect. A
# redirect named MbedTLS would also answer curl's find_package(MbedTLS), which must
# run to read the version, so mbedTLS goes by another name.
lockstep_add_package(NAME MbedTLSSource
        URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
        URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
        EXCLUDE_FROM_ALL
        OPTIONS "ENABLE_PROGRAMS OFF" "ENABLE_TESTING OFF" "MBEDTLS_FATAL_WARNINGS OFF")

# curl keeps a CURL::mbedtls target that already exists. Unlike the imported
# target it would make, an alias is visible from the top-level directory, so the
# library cache sees mbedTLS among libcurl's links.
add_library(CURL::mbedtls ALIAS mbedtls)

lockstep_add_package(NAME CURL
        URL https://curl.se/download/curl-8.23.0.tar.gz
        URL_HASH SHA256=e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49
        EXCLUDE_FROM_ALL
        OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_STATIC_LIBS ON"
        "BUILD_CURL_EXE OFF" "BUILD_TESTING OFF" "BUILD_EXAMPLES OFF" "BUILD_LIBCURL_DOCS OFF"
        "BUILD_MISC_DOCS OFF" "ENABLE_CURL_MANUAL OFF" "CURL_DISABLE_INSTALL ON"
        "CURL_ENABLE_EXPORT_TARGET OFF"
        "CURL_DISABLE_DICT ON" "CURL_DISABLE_FTP ON" "CURL_DISABLE_GOPHER ON"
        "CURL_DISABLE_IMAP ON" "CURL_DISABLE_IPFS ON" "CURL_DISABLE_LDAP ON" "CURL_DISABLE_LDAPS ON"
        "CURL_DISABLE_MQTT ON" "CURL_DISABLE_POP3 ON" "CURL_DISABLE_RTSP ON" "CURL_DISABLE_SMTP ON"
        "CURL_DISABLE_TELNET ON" "CURL_DISABLE_TFTP ON"
        "CURL_USE_MBEDTLS ON" "CURL_USE_OPENSSL OFF"
        "USE_NGHTTP2 OFF" "USE_LIBIDN2 OFF" "CURL_USE_LIBPSL OFF"
        "CURL_USE_LIBSSH2 OFF" "CURL_ZLIB OFF" "CURL_BROTLI OFF" "CURL_ZSTD OFF"
        "CURL_USE_PKGCONFIG OFF" "_CURL_PREFILL ON"
        "CURL_CA_BUNDLE /etc/ssl/certs/ca-certificates.crt"
        "CMAKE_POLICY_DEFAULT_CMP0125 NEW" "CMAKE_POLICY_DEFAULT_CMP0126 NEW"
        "MBEDTLS_INCLUDE_DIR ${MbedTLSSource_SOURCE_DIR}/include"
        "MBEDTLS_LIBRARY mbedtls" "MBEDX509_LIBRARY mbedx509" "MBEDCRYPTO_LIBRARY mbedcrypto"
        # mbedTLS 3.6 builds DES by default; curl would test for it by linking
        # an installed mbedTLS.
        "HAVE_MBEDTLS_DES_CRYPT_ECB 1")
