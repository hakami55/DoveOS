/* Single-process desktop: keeps the verified keyboard, Files and VOUT paths. */
static void desktop_v2_icon(struct app *a,int x,int y,int kind,uint32_t color) {
    if(kind==0){
        rect(a,x,y+12,26,12,color);rect(a,x,y+22,62,40,color);
        rect(a,x+4,y+27,54,30,BG);
    }else if(kind==1){
        for(int row=0;row<58;row++)for(int col=0;col<58;col++){
            int dx=col-29,dy=row-29,ex=col-41,ey=row-18;
            if(dx*dx+dy*dy<27*27&&ex*ex+ey*ey>24*24)rect(a,x+col,y+row,1,1,color);
        }
    }else if(kind==2){
        rect(a,x,y+6,62,40,color);rect(a,x+4,y+10,54,32,BG);
        rect(a,x+28,y+46,6,10,color);rect(a,x+15,y+56,32,4,color);
    }else if(kind==3){
        for(int i=0;i<3;i++){
            rect(a,x,y+10+i*20,60,3,color);
            rect(a,x+12+i*12,y+4+i*20,8,15,color);
        }
    }else{
        rect(a,x,y+16,62,38,color);rect(a,x+5,y+21,52,28,BG);
        rect(a,x+13,y+29,18,5,color);rect(a,x+20,y+23,5,18,color);
        rect(a,x+42,y+27,6,6,color);rect(a,x+50,y+37,6,6,color);
    }
}
static void desktop_v2_home(struct app *a) {
    for(int y=0;y<HEIGHT;y++){
        unsigned v=(unsigned)y*12/HEIGHT;
        rect(a,0,y,WIDTH,1,0x000c1824u+(v<<16)+(v<<8)+v);
    }
    rect(a,0,0,WIDTH,90,0x0012222fu);
    rect(a,30,27,6,34,CYAN);
    label(a,52,22,"DoveOS v2",FG,3,30);
    struct network_info net=network_info();
    char connection[48];
    if(net.ethernet)snprintf(connection,sizeof connection,"Ethernet connected");
    else if(net.wifi)snprintf(connection,sizeof connection,"Wi-Fi %s connected",net.mhz>=4900?"5 GHz":"2.4 GHz");
    else snprintf(connection,sizeof connection,"Offline");
    label(a,904,33,connection,CYAN,1,34);
    if(net.wifi)signal_bars(a,1194,27,net.bars);
    label(a,58,110,"Your workspace",FG,2,40);
    label(a,914,115,"USB desktop / preview 1",MUTED,1,34);
    static const int xx[]={58,646,58,646,58},yy[]={150,150,290,290,430};
    static const char *titles[]={"Files","Moonlight","HDMI In","Settings","Games"};
    static const char *details[]={"Documents, pictures, music and video","Connect to your PC","Open a USB video capture device","Network, Bluetooth, display and sound","Browse your game systems"};
    static const char *shortcuts[]={"F1","F3","F4","F6","F7"};
    static const uint32_t colors[]={GOLD,CYAN,GREEN,0x00c4a4efu,ORANGE};
    for(int i=0;i<5;i++){
        int x=xx[i],y=yy[i],w=i==4?1164:558,selected=i==a->home_selected;
        rect(a,x,y,w,112,selected?0x00234351u:0x00162532u);
        rect(a,x,y,4,112,selected?CYAN:0x00263b48u);
        desktop_v2_icon(a,x+22,y+20,i,colors[i]);
        label(a,x+106,y+23,titles[i],FG,2,24);
        label(a,x+106,y+68,details[i],MUTED,1,51);
        label(a,x+w-42,y+17,shortcuts[i],selected?CYAN:MUTED,1,3);
    }
    label(a,58,579,"Arrows or touchpad to choose  |  Enter to open",MUTED,1,90);
    rect(a,0,654,WIDTH,66,0x0012222fu);
    label(a,30,676,"Workspace",CYAN,2,12);
    label(a,256,682,"Buildroot Linux  |  Existing dock hardware support",MUTED,1,70);
    label(a,1080,682,"F10: exit",MUTED,1,16);
    for(int row=0;row<18;row++)rect(a,a->cursor_x,a->cursor_y+row,1+row/2,1,FG);
}
