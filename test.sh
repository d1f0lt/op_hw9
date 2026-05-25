#!/bin/bash
set -euo pipefail

FAILURES=0
ok()   { echo "[OK]   $*"; }
fail() { echo "[FAIL] $*"; FAILURES=$((FAILURES+1)); }

DIR="$(cd "$(dirname "$0")" && pwd)"
WORK="$DIR/testdir"
IMG="$WORK/ext2.img"
MNT="$WORK/mnt"
SUMS="$WORK/checksums.txt"

mkdir -p "$WORK" "$MNT"

echo "=== Creating ext2 image (block size 2048) ==="
truncate --size 64M "$IMG"
mkfs.ext2 -b 2048 -F "$IMG"

echo "=== Mounting ==="
sudo mount -t ext2 "$IMG" "$MNT"
trap 'mountpoint -q "$MNT" 2>/dev/null && sudo umount "$MNT"' EXIT
sudo chmod a+rwx "$MNT"

echo "=== Creating test files ==="
echo "Hello ext2 world!" > "$MNT/small.txt"
mkdir -p "$MNT/dir1/subdir" "$MNT/dir2"
echo "file inside dir1" > "$MNT/dir1/file1.txt"
echo "file inside subdir" > "$MNT/dir1/subdir/file2.txt"
dd if=/dev/urandom of="$MNT/bigfile.bin" bs=2048 count=100 status=none
dd if=/dev/urandom of="$MNT/sparse.bin" bs=1 count=4096 seek=$((5*1024*1024*1024)) conv=notrunc status=none

echo "=== Computing checksums ==="
: > "$SUMS"
for f in small.txt dir1/file1.txt dir1/subdir/file2.txt bigfile.bin sparse.bin; do
    sha512sum "$MNT/$f" >> "$SUMS"
done
cat "$SUMS"

echo "=== Recording inode numbers ==="
declare -A INODES
for f in small.txt dir1 dir1/file1.txt dir1/subdir dir1/subdir/file2.txt dir2 bigfile.bin sparse.bin; do
    ino=$(stat -c '%i' "$MNT/$f")
    INODES["$f"]=$ino
    echo "  $f -> inode $ino"
done
ROOT_INO=2

echo "=== Recording directory listings ==="
declare -A DIR_LIST
for d in . dir1 dir1/subdir dir2; do
    if [ "$d" = "." ]; then dirpath="$MNT"; else dirpath="$MNT/$d"; fi
    listing=""
    for entry in "$dirpath"/* "$dirpath"/. "$dirpath"/.. ; do
        [ -e "$entry" ] || [ -L "$entry" ] || continue
        listing+="$(stat -c '%i' "$entry") $(basename "$entry")"$'\n'
    done
    DIR_LIST["$d"]="$listing"
done

echo "=== Unmounting ==="
sudo umount "$MNT"
trap - EXIT

test_on_device() {
    local DEV="$1" DEVNAME="$2"
    echo ""
    echo "=== Testing on: $DEVNAME ($DEV) ==="

    echo "--- getinodeinfo ---"
    for f in small.txt bigfile.bin sparse.bin dir1; do
        echo "  ${INODES[$f]} ($f):"
        "$DIR/getinodeinfo" "$DEV" "${INODES[$f]}"
    done

    echo "--- getinodedata: checksum verification ---"
    while IFS= read -r line; do
        expected=$(echo "$line" | awk '{print $1}')
        fpath=$(echo "$line" | awk '{print $2}')
        ino=""
        for key in "${!INODES[@]}"; do
            [ "$MNT/$key" = "$fpath" ] && ino=${INODES[$key]} && break
        done
        [ -z "$ino" ] && { fail "no inode for $fpath"; continue; }
        actual=$("$DIR/getinodedata" "$DEV" "$ino" | sha512sum | awk '{print $1}')
        if [ "$expected" = "$actual" ]; then
            ok "checksum $(basename "$fpath") (inode $ino)"
        else
            fail "checksum $(basename "$fpath") (inode $ino)"
        fi
    done < "$SUMS"

    echo "--- parsedirentry ---"
    for d in . dir1 dir1/subdir dir2; do
        dino=$( [ "$d" = "." ] && echo $ROOT_INO || echo ${INODES[$d]} )
        dlabel=$( [ "$d" = "." ] && echo "/" || echo "$d" )
        parsed=$("$DIR/getinodedata" "$DEV" "$dino" | "$DIR/parsedirentry")
        echo "$parsed"
        ref="${DIR_LIST[$d]}"
        all_ok=1
        while IFS= read -r refline; do
            [ -z "$refline" ] && continue
            ref_ino=$(echo "$refline" | awk '{print $1}')
            ref_name=$(echo "$refline" | awk '{print $2}')
            echo "$parsed" | grep -q "$ref_ino.*$ref_name" || { fail "missing $ref_name in $dlabel"; all_ok=0; }
        done <<< "$ref"
        [ "$all_ok" = 1 ] && ok "directory $dlabel"
    done
}

test_on_device "$IMG" "file image"

echo "=== Setting up loop device ==="
LOOP=$(sudo losetup -f)
sudo losetup "$LOOP" "$IMG"
sudo chmod o+r "$LOOP"

test_on_device "$LOOP" "loop device"

echo "=== losetup -a ==="
losetup -a

echo "=== lsblk -o name,size,fstype ==="
lsblk -o name,size,fstype

echo "=== Detaching loop device ==="
sudo losetup -d "$LOOP"

echo ""
if [ "$FAILURES" -eq 0 ]; then
    echo "All tests passed."
else
    echo "$FAILURES test(s) failed."
    exit 1
fi
