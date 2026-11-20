#pragma once
enum { DESK_TOOLBAR=1, DESK_KEYBOARD=2, DESK_MORE=3 };
static int desktop_view_count(int view) {return view==DESK_KEYBOARD?40:view==DESK_MORE?8:6;}
static const char *desktop_tool_label(int view,int index) {
    static const char *basic[]={"Keyboard","Address / Search","Back","Forward","Switch window","More actions"};
    static const char *more[]={"Windows Explorer","Refresh","Home","Windows","Next tab","Fullscreen","Windows keyboard","Notification area"};
    return view==DESK_MORE?more[index]:basic[index];
}
static const char *desktop_key_label(int page,bool caps,int index) {
    static const char *letters[]={"q","w","e","r","t","y","u","i","o","p","a","s","d","f","g","h","j","k","l","\xc3\xa7","z","x","c","v","b","n","m",",",".","?"};
    static const char *upper[]={"Q","W","E","R","T","Y","U","I","O","P","A","S","D","F","G","H","J","K","L","\xc3\x87","Z","X","C","V","B","N","M",",",".","?"};
    static const char *accents[]={"\xc3\xa1","\xc3\xa0","\xc3\xa2","\xc3\xa3","\xc3\xa9","\xc3\xaa","\xc3\xad","\xc3\xb3","\xc3\xb4","\xc3\xb5","\xc3\xba","\xc3\xbc","\xc3\xa7","\xc3\x81","\xc3\x80","\xc3\x82","\xc3\x83","\xc3\x89","\xc3\x8a","\xc3\x8d","\xc3\x93","\xc3\x94","\xc3\x95","\xc3\x9a","\xc3\x9c","\xc3\x87","@","/",":","-"};
    static const char *symbols[]={"1","2","3","4","5","6","7","8","9","0","!","@","#","$","%","&","*","(",")","_","+","-","=","/","\\",":",";","\"","'","?"};
    static const char *special[]={"Space","Bksp","Enter","Caps","Page","OSK","Taskbar","Tab","Win","Close"};
    if(index<0 || index>=40)return "";
    if(index>=30)return special[index-30];
    return page==1?accents[index]:page==2?symbols[index]:caps?upper[index]:letters[index];
}
