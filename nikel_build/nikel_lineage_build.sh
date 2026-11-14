#!/bin/bash
# ---------------------------------------------------------------------------
# LineageOS 15.1 for Redmi Note 4 MTK (nikel) - build helper for WSL Ubuntu 22.04
#
# Keep next to this script (the project folder):
#   nikel-lineage-trees.tar.gz   device/xiaomi/nikel
#   nikel-Image.gz-dtb           our kernel (from nikel_setup_and_build.sh)
#   stock-3x64/                  MIUI /system copy (nikel-pull-stock.bat)
# and SHIFT's unpacked vendor image in ~/nikel/shift-vendor.
#
# Stages (run in this order, each can be repeated):
#   bash nikel_lineage_build.sh setup     packages, repo tool             (minutes)
#   bash nikel_lineage_build.sh sync      download LineageOS 15.1       (hours, ~60 GB)
#   bash nikel_lineage_build.sh extract   device tree + blobs + kernel  (minutes)
#   bash nikel_lineage_build.sh build     the ROM zip                   (hours first time)
#   bash nikel_lineage_build.sh boot      only boot.img, after a kernel change (minutes)
#
# Nothing here touches the phone.
# ---------------------------------------------------------------------------
set -e
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOS="${LOS:-$HOME/lineage}"
SHIFT_VENDOR="${SHIFT_VENDOR:-$HOME/nikel/shift-vendor}"
STOCK="${STOCK:-$HERE/stock-3x64}"
export PATH="$HOME/bin:$PATH"

step() { echo; echo "=== $* ==="; }

case "$1" in
setup)
    step "Packages (Lineage 15.1 build deps on Ubuntu 22.04)"
    sudo apt-get update
    sudo apt-get install -y openjdk-8-jdk bc bison build-essential curl flex \
        g++-multilib gcc-multilib git gnupg gperf imagemagick lib32readline-dev \
        lib32z1-dev liblz4-tool libncurses5 libncurses5-dev libsdl1.2-dev libssl-dev \
        libxml2 libxml2-utils lzop pngcrush rsync schedtool squashfs-tools xsltproc \
        zip zlib1g-dev python2 python3 unzip git-lfs p7zip-full
    git lfs install
    mkdir -p "$HOME/bin"
    # Lineage 15.1's build scripts need "python" = Python 2, and its prebuilt
    # clang++ wrapper hard-codes #!/usr/bin/python (Ubuntu 22.04 has none).
    ln -sf "$(command -v python2)" "$HOME/bin/python"
    [ -e /usr/bin/python ] || sudo ln -s /usr/bin/python2 /usr/bin/python
    /usr/bin/python --version
    if [ ! -x "$HOME/bin/repo" ]; then
        curl -fsSL https://storage.googleapis.com/git-repo-downloads/repo -o "$HOME/bin/repo"
        chmod +x "$HOME/bin/repo"
    fi
    # Java 8 is required by Android 8.1
    sudo update-alternatives --set java /usr/lib/jvm/java-8-openjdk-amd64/jre/bin/java || true
    sudo update-alternatives --set javac /usr/lib/jvm/java-8-openjdk-amd64/bin/javac || true
    java -version 2>&1 | head -1
    git config --global user.name >/dev/null || git config --global user.name "nikel builder"
    git config --global user.email >/dev/null || git config --global user.email "nikel@localhost"
    echo "setup done"
    ;;
sync)
    step "Download LineageOS 15.1 into $LOS"
    mkdir -p "$LOS" && cd "$LOS"
    [ -d .repo ] || repo init -u https://github.com/LineageOS/android.git -b lineage-15.1 --depth=1
    repo sync -c -j8 --force-sync --no-tags --no-clone-bundle
    # WebView prebuilts are Git LFS files: make sure the real APKs are there,
    # not 130-byte placeholders
    for a in arm arm64 x86 x86_64; do
        (cd "external/chromium-webview/prebuilt/$a" && git lfs pull)
    done
    ls -l external/chromium-webview/prebuilt/arm64/webview.apk
    echo "sync done"
    ;;
extract)
    step "Device tree"
    cd "$LOS"
    [ -f "$HERE/nikel-lineage-trees.tar.gz" ] || { echo "missing nikel-lineage-trees.tar.gz"; exit 1; }
    rm -rf device/xiaomi/nikel
    tar -xzf "$HERE/nikel-lineage-trees.tar.gz" -C "$LOS"
    step "Source patches (device/xiaomi/nikel/patches)"
    PD="$LOS/device/xiaomi/nikel/patches"
    if [ -d "$PD" ]; then
        while IFS= read -r p; do
            rel="${p#$PD/}"; repo="$(dirname "$rel")"
            if git -C "$LOS/$repo" apply -R --check "$p" 2>/dev/null; then
                echo "already applied: $rel"
            elif git -C "$LOS/$repo" apply --check "$p"; then
                git -C "$LOS/$repo" apply "$p" && echo "applied: $rel"
            else
                echo "PATCH FAILED: $rel"; exit 1
            fi
        done < <(find "$PD" -name '*.patch' | sort)
    fi
    step "Kernel (prebuilt)"
    [ -f "$HERE/nikel-Image.gz-dtb" ] || { echo "missing nikel-Image.gz-dtb"; exit 1; }
    cp "$HERE/nikel-Image.gz-dtb" device/xiaomi/nikel/prebuilt/Image.gz-dtb
    step "Blobs -> vendor/xiaomi/nikel"
    if [ ! -f "$SHIFT_VENDOR/build.prop" ]; then
        echo "unpacking SHIFT's vendor.img to $SHIFT_VENDOR"
        [ -f "$HERE/vendor.img" ] || { echo "missing $HERE/vendor.img"; exit 1; }
        mkdir -p "$(dirname "$SHIFT_VENDOR")"
        7z x -y "$HERE/vendor.img" -o"$SHIFT_VENDOR" >/dev/null || true
        [ -f "$SHIFT_VENDOR/build.prop" ] || { echo "unpacking vendor.img failed"; exit 1; }
    fi
    [ -d "$STOCK/system" ] || { echo "missing $STOCK/system"; exit 1; }
    cd device/xiaomi/nikel
    ./extract-files.sh "$SHIFT_VENDOR" "$STOCK" 2>&1 | tee "$HERE/extract.log"
    nf=$(grep -c 'file not found' "$HERE/extract.log" || true)
    echo "files not found: $nf (should be 0; details in extract.log)"
    ;;
build)
    step "Build lineage_nikel-userdebug"
    cd "$LOS"
    export LC_ALL=C                      # Lineage 15.1's prebuilt flex crashes on newer locales
    # ccache off by default: it does not speed up a first build, and Ubuntu
    # 22.04's ccache 4 fails on Android 8.1's relative compiler paths.
    # For later rebuilds: NIKEL_CCACHE=1 bash nikel_lineage_build.sh build
    # (uses the ccache that ships with Lineage 15.1, if present).
    unset USE_CCACHE CCACHE_EXEC
    if [ "${NIKEL_CCACHE:-0}" = "1" ] && [ -x prebuilts/misc/linux-x86/ccache/ccache ]; then
        export USE_CCACHE=1
        prebuilts/misc/linux-x86/ccache/ccache -M 25G >/dev/null
        echo "ccache: on (Lineage prebuilt)"
    else
        echo "ccache: off"
    fi
    # Jack (Android 8.1's Java compiler) runs as a local server that only speaks
    # TLS 1.0, which current OpenJDK 8 disables. Allow it for the Jack server JVM
    # only, through an override file; the system-wide java.security is untouched.
    JACK_SEC="$HOME/.jack-server-tls.security"
    cat > "$JACK_SEC" <<'JSEC'
# Used only by the Jack server of the Lineage 15.1 build (localhost).
# Same as the OpenJDK 8 default minus TLSv1/TLSv1.1.
jdk.tls.disabledAlgorithms=SSLv3, RC4, DES, MD5withRSA, DH keySize < 1024, EC keySize < 224, 3DES_EDE_CBC, anon, NULL
JSEC
    export ANDROID_JACK_VM_ARGS="-Dfile.encoding=UTF-8 -XX:+TieredCompilation -Xmx6g -Djava.security.properties=$JACK_SEC"
    set +e
    source build/envsetup.sh
    lunch lineage_nikel-userdebug || exit 1
    # Lineage's envsetup.sh turns on the system ccache by itself; undo that
    # here (after envsetup/lunch) unless NIKEL_CCACHE=1 asked for ccache.
    if [ "${NIKEL_CCACHE:-0}" != "1" ]; then
        unset USE_CCACHE CCACHE_EXEC
    fi
    mka bacon -j"$(nproc)" 2>&1 | tee "$HERE/lineage-build.log"
    rc=${PIPESTATUS[0]}
    set -e
    if [ "$rc" -eq 0 ] && ls out/target/product/nikel/lineage-15.1-*-nikel.zip >/dev/null 2>&1; then
        echo; echo "BUILD OK:"; ls -l out/target/product/nikel/lineage-15.1-*-nikel.zip
        # Copy the new zip next to this script, so an older copy there is
        # never installed by mistake, and show both so the times can be compared.
        cp -f out/target/product/nikel/lineage-15.1-*-nikel.zip "$HERE/"
        echo "Copied to the project folder:"; ls -l "$HERE"/lineage-15.1-*-nikel.zip
    else
        echo; echo "BUILD FAILED (exit $rc). Log: $HERE/lineage-build.log"
        grep -nE "error:|FAILED:|Error [0-9]" "$HERE/lineage-build.log" | tail -20
        exit 1
    fi
    ;;
boot)
    step "Rebuild only boot.img with the current kernel (system stays as installed)"
    cd "$LOS"
    [ -f "$HERE/nikel-Image.gz-dtb" ] || { echo "missing nikel-Image.gz-dtb"; exit 1; }
    cp "$HERE/nikel-Image.gz-dtb" device/xiaomi/nikel/prebuilt/Image.gz-dtb
    export LC_ALL=C
    set +e
    source build/envsetup.sh
    lunch lineage_nikel-userdebug || exit 1
    unset USE_CCACHE CCACHE_EXEC
    mka bootimage 2>&1 | tee "$HERE/lineage-bootimage.log"
    rc=${PIPESTATUS[0]}
    set -e
    if [ "$rc" -eq 0 ] && [ -f out/target/product/nikel/boot.img ]; then
        cp out/target/product/nikel/boot.img "$HERE/nikel-lineage-boot.img"
        echo; echo "BOOT OK: $HERE/nikel-lineage-boot.img"; ls -l "$HERE/nikel-lineage-boot.img"
        echo "Test it with: fastboot boot nikel-lineage-boot.img  (RAM only)"
    else
        echo "BOOT IMAGE BUILD FAILED (exit $rc). Log: $HERE/lineage-bootimage.log"; exit 1
    fi
    ;;
*)
    sed -n '2,20p' "$0"
    exit 1
    ;;
esac
