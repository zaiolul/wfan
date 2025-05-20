#!/bin/bash

[ $(id -u) -ne 0 ] && {
    echo "Must be run as root"
    exit 1
}
PHYS=()
SELECTED_PHY=
MONITOR_DEV=

stop_proc() {
    #stop network manager stuff, cause it will interrupt
    [ -n "$(nmcli -v)" ] && {
    #    nmcli dev set $SELECTED_DEV managed no
        nmcli radio wifi off
    }

    #if still active
    [ -n "$(ps -e | grep wpa_supplicant)" ] && {
        killall wpa_supplicant
    }

    [ -n "$(rfkill --version)" ] && {
        RFKILL_IDX=$(rfkill list -o ID,DEVICE | grep $SELECTED_PHY | awk '{print $1}')
        [ -n "$RFKILL_IDX" ] &&  rfkill unblock $RFKILL_IDX
    }
}

get_phy() {
    [ -d "/sys/class/ieee80211" ] && {
        PHYS=($(ls /sys/class/ieee80211))
    }

    [ ${#PHYS[@]} -eq 0 ] && {
        echo "No wifi devices found"
        exit 1
    }

    if [ ${#PHYS[@]} -gt 1 ]; then 
        local idx=1
        local mac=
        local select_idx=
        echo "Select device for capture:"
        for phy in "${PHYS[@]}"; do
            mac=$(cat /sys/class/ieee80211/$phy/macaddress)
            echo -e "\t$idx: $phy ($mac)"
            idx=$((idx + 1))
        done

        read -p '> ' select_idx

        if ! [[ "$select_idx" =~ [0-9] ]] || [[ $select_idx -gt ${#PHYS[@]} || $select_idx -lt 1 ]]; then 
            echo "Bad selection"
            exit 1;
        fi
        SELECTED_PHY=${PHYS[select_idx - 1]}
    else
        SELECTED_PHY=${PHYS[0]}
    fi

    echo "Selected ($SELECTED_PHY)"
}
setup_monitor() {
    local prefix=/sys/class/ieee80211/$SELECTED_PHY/device/net
    for iface in $(ls $prefix); do
        case $(cat $prefix/$iface/type) in
            803)
                echo "$SELECTED_PHY already has a monitor interface \"$iface\", continue with that."
                MONITOR_DEV=$iface
            ;;
            *)
                ip link set dev $iface down
            ;;
        esac
    done

    if [ -z "$MONITOR_DEV" ]; then
        echo "Creating monitor interface"
        MONITOR_DEV="mon${SELECTED_PHY:3}"
        iw phy $SELECTED_PHY interface add $MONITOR_DEV type monitor
    fi

    ip link set $MONITOR_DEV up

    echo "Monitor interface $MONITOR_DEV is set up."
}

get_phy

stop_proc

setup_monitor
