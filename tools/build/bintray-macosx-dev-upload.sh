#! /bin/bash

set -e
set -x

user_info="$BINTRAY_USER:$BINTRAY_API_KEY"
repo="quiterss2-development"
package="MacOSX-x64-dev"
build_dir="."
build_ver="build${TRAVIS_BUILD_NUMBER}-${TRAVIS_BRANCH}"
file_path="QuiteRSS-$build_ver.dmg"
build_file="$(ls ${build_dir}/*.dmg | head -n 1)"
upload_url="https://api.bintray.com/content/quiterss/$repo/$package/$build_ver/$file_path?override=1&publish=1"

if [ -z "$build_file" ]; then
  echo "ERROR: Cannot upload. No dmg file found in $build_dir"
  exit 1
else
  echo "Uploading $file to $upload_url"
  curl -T "${build_file}" -u"${user_info}" "${upload_url}" || exit 1
fi
