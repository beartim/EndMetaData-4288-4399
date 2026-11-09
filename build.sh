#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
root=$PWD
mode=${1:-host}
case "$mode" in
    host|test|ios) ;;
    *) echo 'Usage: bash build.sh [host|test|ios]' >&2; exit 2 ;;
esac

version=0.8.0a8
dependencies="$root/build/deps"
archive="$dependencies/leveldb-mcpe-$version.tar.gz"
leveldb="$dependencies/leveldb-mcpe-$version"
mkdir -p "$dependencies"
if [[ ! -f "$archive" ]]; then
    curl --fail --silent --show-error --location --retry 3 --max-time 120 \
        "https://codeload.github.com/Amulet-Team/leveldb-mcpe/tar.gz/refs/tags/$version" -o "$archive.part"
    mv "$archive.part" "$archive"
fi
printf '%s  %s\n' ad0939493d54467ee8ad21516be115688bae9c89b6cfaf8ab1f568fd8a002a7f "$archive" | shasum -a 256 -c -
if [[ ! -d "$leveldb" ]]; then
    tar -xzf "$archive" -C "$dependencies"
fi

output="$root/build/host"
compiler=("${CXX:-c++}")
flags=(-std=c++17 -O2 -DNDEBUG -pthread -DLEVELDB_PLATFORM_POSIX -DDLLX= -isystem "$leveldb" -isystem "$leveldb/include")
if [[ $(uname -s) == Darwin ]]; then
    flags+=(-DOS_MACOSX)
fi
if [[ "$mode" == ios ]]; then
    output="$root/build/ios"
    compiler=(xcrun --sdk iphoneos clang++)
    flags+=(-target arm64-apple-ios13.0 -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)")
fi

sources=(
    "$leveldb"/db/{builder,db_impl,db_iter,dbformat,filename,log_reader,log_writer,memtable,repair,table_cache,version_edit,version_set,write_batch,zlib_compressor}.cc
    "$leveldb"/table/{block,block_builder,filter_block,format,iterator,merger,table,table_builder,two_level_iterator}.cc
    "$leveldb"/util/{arena,bloom,cache,coding,comparator,crc32c,env,env_posix,filter_policy,hash,histogram,logging,options,status}.cc
    "$leveldb"/port/{port_posix,port_posix_sse}.cc
)
objects="$output/objects"
mkdir -p "$objects"
(
    cd "$objects"
    "${compiler[@]}" "${flags[@]}" -c "${sources[@]}"
)

binary="$output/endmetadata-4288-4399"
"${compiler[@]}" "${flags[@]}" -Wall -Wextra main.cc "$objects"/*.o -lz -o "$binary"
if [[ "$mode" == test ]]; then
    fixture="$output/create-fixture"
    "${compiler[@]}" "${flags[@]}" tests/fixture.cc "$objects"/*.o -lz -o "$fixture"
    python3 tests/check.py "$binary" "$fixture"
elif [[ "$mode" == ios ]]; then
    [[ $(xcrun lipo -archs "$binary") == arm64 ]]
    xcrun vtool -show-build "$binary" | tee "$output/platform.txt"
    awk '$1 == "platform" && $2 == "IOS" { ios = 1 }
         $1 == "minos" && $2 == "13.0" { ios13 = 1 }
         END { exit !(ios && ios13) }' "$output/platform.txt"

    xcrun strip -x "$binary"
    codesign --force --sign - --timestamp=none "$binary"
    codesign --verify --strict --verbose=2 "$binary"
    cp "$leveldb/LICENSE" "$output/LevelDB-LICENSE.txt"
    package="$output/EndMetaData-4288-4399-iOS13-arm64.zip"
    rm -f "$package"
    zip -j -X "$package" "$binary" "$output/LevelDB-LICENSE.txt"
    shasum -a 256 "$package"
fi
