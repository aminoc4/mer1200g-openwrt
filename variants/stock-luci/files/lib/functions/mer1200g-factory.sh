#!/bin/sh
# Read-only identity parser for the proposed image, never run on current hardware here.
mer1200g_format_factory_mac() {
 local raw="$1" first
 [ "${#raw}" = 12 ] || return 1
 case "$raw" in *[!0-9A-Fa-f]*) return 1;;esac
 raw=$(printf '%s' "$raw" | tr A-F a-f)
 case "$raw" in 000000000000|ffffffffffff) return 1;;esac
 first=$(printf '%.2s' "$raw")
 [ "$((0x$first & 1))" = 0 ] || return 1
 printf '%s' "$raw" | sed 's/../&:/g;s/:$//'
}
mer1200g_factory_lan_mac() {
 local p label index selected= count=0 raw
 for p in /sys/class/mtd/mtd[0-9]*;do
  [ -d "$p" ] || continue
  index=${p##*/};index=${index#mtd}
  case "$index" in ''|*[!0-9]*) continue;;esac
  read -r label < "$p/name" || continue
  [ "$label" = tddp ] || continue
  count=$((count+1));selected=$p
 done
 [ "$count" = 1 ] || return 1
 [ "$(cat "$selected/size")" = 65536 ] || return 1
 [ "$(cat "$selected/offset")" = "$((0xd50000))" ] || return 1
 index=${selected##*/}
 [ -c "/dev/$index" ] || return 1
 raw=$(dd if="/dev/$index" bs=1 skip=40 count=12 2>/dev/null) || return 1
 mer1200g_format_factory_mac "$raw"
}
