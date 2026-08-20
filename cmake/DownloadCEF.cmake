# Based on the official chromiumembedded/cef-project DownloadCEF.cmake.
# Downloads the requested standard CEF binary distribution and verifies SHA1.
function(DownloadCEF platform channel version download_dir)
  if(channel STREQUAL "beta")
    set(channel_part "_beta")
  else()
    set(channel_part "")
  endif()

  set(CEF_DISTRIBUTION "cef_binary_${version}_${platform}${channel_part}")
  set(CEF_DOWNLOAD_DIR "${download_dir}")
  set(CEF_ROOT "${CEF_DOWNLOAD_DIR}/${CEF_DISTRIBUTION}" CACHE INTERNAL "CEF_ROOT")

  if(NOT IS_DIRECTORY "${CEF_ROOT}")
    file(MAKE_DIRECTORY "${CEF_DOWNLOAD_DIR}")
    set(CEF_DOWNLOAD_FILENAME "${CEF_DISTRIBUTION}.tar.bz2")
    set(CEF_DOWNLOAD_PATH "${CEF_DOWNLOAD_DIR}/${CEF_DOWNLOAD_FILENAME}")

    if(NOT EXISTS "${CEF_DOWNLOAD_PATH}")
      set(CEF_DOWNLOAD_URL "https://cef-builds.spotifycdn.com/${CEF_DOWNLOAD_FILENAME}")
      string(REPLACE "+" "%2B" CEF_DOWNLOAD_URL_ESCAPED "${CEF_DOWNLOAD_URL}")

      message(STATUS "Downloading CEF SHA1...")
      file(DOWNLOAD "${CEF_DOWNLOAD_URL_ESCAPED}.sha1"
                    "${CEF_DOWNLOAD_PATH}.sha1"
                    STATUS SHA1_STATUS)
      list(GET SHA1_STATUS 0 SHA1_CODE)
      if(NOT SHA1_CODE EQUAL 0)
        message(FATAL_ERROR "Unable to download CEF SHA1: ${SHA1_STATUS}")
      endif()
      file(READ "${CEF_DOWNLOAD_PATH}.sha1" CEF_SHA1)
      string(STRIP "${CEF_SHA1}" CEF_SHA1)

      message(STATUS "Downloading ${CEF_DOWNLOAD_FILENAME}...")
      file(DOWNLOAD "${CEF_DOWNLOAD_URL_ESCAPED}"
                    "${CEF_DOWNLOAD_PATH}"
                    EXPECTED_HASH "SHA1=${CEF_SHA1}"
                    SHOW_PROGRESS
                    STATUS DOWNLOAD_STATUS)
      list(GET DOWNLOAD_STATUS 0 DOWNLOAD_CODE)
      if(NOT DOWNLOAD_CODE EQUAL 0)
        file(REMOVE "${CEF_DOWNLOAD_PATH}")
        message(FATAL_ERROR "Unable to download CEF: ${DOWNLOAD_STATUS}")
      endif()
    endif()

    message(STATUS "Extracting ${CEF_DOWNLOAD_FILENAME}...")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E tar xzf "${CEF_DOWNLOAD_PATH}"
      WORKING_DIRECTORY "${CEF_DOWNLOAD_DIR}"
      RESULT_VARIABLE EXTRACT_RESULT)
    if(NOT EXTRACT_RESULT EQUAL 0 OR NOT IS_DIRECTORY "${CEF_ROOT}")
      message(FATAL_ERROR "CEF archive extraction failed: ${EXTRACT_RESULT}")
    endif()
  endif()
endfunction()
