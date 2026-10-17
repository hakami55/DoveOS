#!/bin/sh
# Wi-Fi lease plus Internet-time host routes; wired debug is retained.
[ "$interface" = wlan0 ] || exit 1
case "$1" in
 bound|renew)
  case "$ip" in ''|*[!0-9.]*) exit 1;; esac
  mask=${subnet:-255.255.255.0}
  prefix=$(awk -v mask="$mask" 'BEGIN {split(mask,m,".");bits=0;zero=0;for(i=1;i<=4;i++)for(b=128;b>=1;b/=2){on=int(m[i]/b)%2;if(on&&zero)exit 1;if(on)bits++;else zero=1;}if(bits<1||bits>30)exit 1;print bits}') || exit 1
  /bin/busybox ip addr add "$ip/$prefix" dev wlan0 2>/dev/null || :
  printf '%s/%s\n' "$ip" "$prefix" > /tmp/doveos-wifi/lease
  router_ip=$(awk -v list="$router" -v own="$ip" -v mask="$mask" 'BEGIN {split(own,o,".");split(mask,m,".");n=split(list,r," ");for(k=1;k<=n;k++){if(split(r[k],v,".")!=4)continue;ok=1;for(i=1;i<=4;i++){if(v[i]!~/^[0-9]+$/||v[i]>255){ok=0;break;}a=0;b=0;for(bit=128;bit>=1;bit/=2)if(int(m[i]/bit)%2){a+=int(o[i]/bit)%2*bit;b+=int(v[i]/bit)%2*bit;}if(a!=b)ok=0;}if(ok&&r[k]!=own){print r[k];exit;}}}')
  if [ -n "$router_ip" ];then
   printf '%s\n' "$router_ip" > /tmp/doveos-wifi/router
   for peer in 162.159.200.1 162.159.200.123;do /bin/busybox ip route replace "$peer/32" via "$router_ip" dev wlan0 2>/dev/null || :;done
  fi
  ;;
esac
exit 0
