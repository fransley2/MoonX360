#pragma once
enum ui_option_kind { UI_OPTION_OPEN=1,UI_OPTION_CYCLE,UI_OPTION_TOGGLE,UI_OPTION_ACTION,UI_OPTION_INFO };
enum ui_page { UI_HOME,UI_LIBRARY,UI_STREAM,UI_SETTINGS,UI_PAGE_COUNT };
/* Only the four main tabs participate in shoulder-button navigation. */
static int ui_tab_move(int page,int direction) {
    if(page<0||page>=UI_PAGE_COUNT)page=0;
    return (page+(direction<0?UI_PAGE_COUNT-1:1))%UI_PAGE_COUNT;
}
static int ui_grid_move(int selected,int count,int columns,int dx,int dy) {
    if(count<=0)return 0;
    if(selected<0||selected>=count)selected=0;
    int next=selected+dx+dy*columns;
    if(dx<0 && selected%columns==0)return selected;
    if(dx>0 && selected%columns==columns-1)return selected;
    return next>=0&&next<count?next:selected;
}
