#pragma once
#define UI_COVER_LIMIT 12
/* At most two visible rows plus a small prefetch margin. No entire-library load. */
static int ui_cover_window(int selected,int count,int columns,int *first) {
    int row=selected/columns;
    *first=row*columns;
    int n=count-*first;if(n>UI_COVER_LIMIT)n=UI_COVER_LIMIT;
    return n>0?n:0;
}
