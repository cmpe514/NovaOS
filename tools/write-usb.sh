#!/usr/bin/env bash
# Writes the newest ISO to a USB stick (Linux). Refuses anything that is not a whole, removable/USB
# disk, anything mounted, and the disk the running system lives on; asks you to type the device again.
#   sudo tools/write-usb.sh /dev/sdX          ISO=path   PERSIST=1 (add a NOVA-DATA partition in the free space)
# macOS / Windows: use balenaEtcher or Rufus ("DD image" mode) instead — see README.
. "$(dirname "$0")/lib.sh"
NOVA_STEP=usb
dev="${1:-}"
[[ "$(uname -s)" == Linux ]] || die "this script is for Linux. On macOS/Windows write the ISO with balenaEtcher (or Rufus in DD mode)."
[[ $EUID -eq 0 ]] || die "run with sudo: sudo tools/write-usb.sh $dev"
[[ -n "$dev" ]] || die "usage: sudo tools/write-usb.sh /dev/sdX   (list disks: lsblk -d -o NAME,SIZE,MODEL,TRAN,RM)"
need lsblk "sudo apt install util-linux"
id="$(cfg id)"
iso="${ISO:-$(ls -t "$ROOT"/out/"$id"-*.iso 2>/dev/null | head -1 || true)}"
[[ -f "$iso" ]] || die "no ISO in out/ (make iso)"
[[ -b "$dev" ]] || die "$dev is not a block device"
name="$(basename "$(readlink -f "$dev")")"
[[ "$(lsblk -dn -o TYPE "/dev/$name")" == disk ]] || die "$dev is a partition or not a disk; give the whole disk, e.g. /dev/sdb, not /dev/sdb1"
removable="$(cat "/sys/block/$name/removable" 2>/dev/null || echo 0)"
tran="$(lsblk -dn -o TRAN "/dev/$name" | tr -d ' ')"
[[ "$removable" == 1 || "$tran" == usb || "$tran" == mmc ]] || die "$dev is not a removable/USB disk (removable=$removable, transport=${tran:-none}); refusing to overwrite an internal disk"
mounted="$(lsblk -n -o MOUNTPOINTS "/dev/$name" | grep -v '^\s*$' || true)"
[[ -z "$mounted" ]] || die "$dev has mounted partitions:
$mounted
Unmount them first (e.g. udisksctl unmount -b /dev/${name}1)"
root_src="$(findmnt -n -o SOURCE / || true)"
root_disk="$(lsblk -n -o PKNAME "$root_src" 2>/dev/null | head -1 || true)"
[[ "$root_disk" != "$name" ]] || die "$dev holds the running system"
size="$(lsblk -dn -b -o SIZE "/dev/$name")"; iso_size="$(stat -c %s "$iso")"
(( size > iso_size )) || die "$dev ($((size/1000000)) MB) is smaller than the ISO ($((iso_size/1000000)) MB)"

echo
lsblk -o NAME,SIZE,MODEL,TRAN,FSTYPE,LABEL "/dev/$name"
echo
echo "This ERASES EVERYTHING on /dev/$name ($(lsblk -dn -o MODEL "/dev/$name" | xargs) $((size/1000000000)) GB)"
echo "and writes $(basename "$iso")."
read -r -p "Type the device path again to confirm (/dev/$name): " answer
[[ "$answer" == "/dev/$name" ]] || die "confirmation did not match; nothing was written"

log "writing (this takes a few minutes)"
dd if="$iso" of="/dev/$name" bs=4M conv=fsync oflag=direct status=progress
sync
if [[ "${PERSIST:-0}" == 1 ]]; then
  need sfdisk "sudo apt install fdisk"; need mkfs.ext4 "sudo apt install e2fsprogs"
  log "adding a NOVA-DATA partition in the remaining space"
  echo ',,L' | sfdisk --append --no-reread "/dev/$name"
  partprobe "/dev/$name" 2>/dev/null || blockdev --rereadpt "/dev/$name" || true
  udevadm settle
  part="$(lsblk -nr -o NAME,TYPE "/dev/$name" | awk '$2=="part"{p=$1} END{print p}')"
  [[ -n "$part" ]] || die "could not find the new partition"
  mkfs.ext4 -q -F -L NOVA-DATA "/dev/$part"
  log "/dev/$part is the NOVA-DATA partition: notes, files and settings will survive reboots"
fi
sync
log "done — you can remove the stick and boot from it (choose it in the firmware boot menu)"
