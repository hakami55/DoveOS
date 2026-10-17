# Choose a device-owned IPv4 address on the receiver's current Wi-Fi subnet.
# The gateway wins only when the USB-authorized device actually owns it.
function ipv4(s, parts, n, i, value) {
    n=split(s,parts,".");if(n!=4)return -1;
    value=0;
    for(i=1;i<=4;i++){
        if(parts[i]!~/^[0-9]+$/||length(parts[i])>3||parts[i]+0>255)return -1;
        value=value*256+parts[i];
    }
    return value;
}
BEGIN {
    if(split(cidr,network,"/")!=2||network[2]!~/^[0-9]+$/||network[2]<1||network[2]>32)exit 2;
    receiver=ipv4(network[1]);if(receiver<0)exit 2;
    # The dock's BusyBox awk omits optional math/pow support.
    block=1;for(bit=network[2];bit<32;bit++)block*=2;
    subnet=int(receiver/block);
}
{
    value=ipv4($1);
    if(value>=0&&value!=receiver&&int(value/block)==subnet&&!seen[$1]++){
        candidate=$1;count++;
        if($1==gateway)preferred=$1;
    }
}
END {
    if(preferred!="")print preferred;
    else if(count==1)print candidate;
    else exit 2;
}
