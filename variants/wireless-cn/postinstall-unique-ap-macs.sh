#!/bin/sh
# Apply only to this board's freshly generated, disabled default interfaces.
set -eu
. /lib/functions/system.sh
. /lib/functions/mer1200g-factory.sh
base=$(mer1200g_factory_lan_mac)
[ "$(uci -q get wireless.default_radio0.disabled)" = 1 ]
[ "$(uci -q get wireless.default_radio1.disabled)" = 1 ]
m0=$(macaddr_setbit_la "$(macaddr_add "$base" 2)")
m1=$(macaddr_setbit_la "$(macaddr_add "$base" 3)")
[ "$m0" != "$m1" ] && [ "$m0" != "$base" ] && [ "$m1" != "$base" ]
uci set wireless.default_radio0.macaddr="$m0"
uci set wireless.default_radio1.macaddr="$m1"
uci commit wireless
wifi reload
