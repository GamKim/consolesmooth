/* Original vector icons rendered at 4x and downsampled for smooth outlines. */
static void ui_icon(HDC dc,RECT rect,int kind,COLORREF color,COLORREF background){
 /* Follow the button size: toolbar buttons are sized from the host toolbar, not from ui_dpi. */
 int size=(rect.bottom-rect.top)*37/44;if(size<8)size=8;int n=size*4;HDC mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,n,n),oldbmp=SelectObject(mem,bitmap);
 RECT full={0,0,n,n};ui_fill(mem,full,background);SetMapMode(mem,MM_ANISOTROPIC);SetWindowExtEx(mem,32,32,NULL);SetViewportExtEx(mem,n,n,NULL);
 HPEN pen=CreatePen(PS_SOLID,kind==2?2:3,color),oldpen=SelectObject(mem,pen);HGDIOBJ oldbrush=SelectObject(mem,GetStockObject(NULL_BRUSH));
 /* Centred by eye: the front square carries the weight, so the whole shape sits slightly left and up. */
 if(kind==0){ui_line(mem,6,21,4,21);ui_line(mem,4,21,4,4);ui_line(mem,4,4,22,4);ui_line(mem,22,4,22,7);RoundRect(mem,9,9,27,27,3,3);ui_line(mem,16,14,20,18);ui_line(mem,20,18,16,22);}
 else if(kind==1){ui_line(mem,4,8,28,8);ui_line(mem,4,16,28,16);ui_line(mem,4,24,28,24);HBRUSH b=CreateSolidBrush(background);SelectObject(mem,b);RoundRect(mem,9,5,13,11,2,2);RoundRect(mem,20,13,24,19,2,2);RoundRect(mem,9,21,13,27,2,2);SelectObject(mem,GetStockObject(NULL_BRUSH));DeleteObject(b);}
 else {ui_line(mem,10,10,22,22);ui_line(mem,22,10,10,22);}
 SelectObject(mem,oldbrush);SelectObject(mem,oldpen);DeleteObject(pen);SetMapMode(mem,MM_TEXT);
 int oldmode=SetStretchBltMode(dc,HALFTONE);POINT oldorigin;SetBrushOrgEx(dc,0,0,&oldorigin);StretchBlt(dc,(rect.left+rect.right-size)/2,(rect.top+rect.bottom-size)/2,size,size,mem,0,0,n,n,SRCCOPY);SetBrushOrgEx(dc,oldorigin.x,oldorigin.y,NULL);SetStretchBltMode(dc,oldmode);SelectObject(mem,oldbmp);DeleteObject(bitmap);DeleteDC(mem);
}
static void ui_draw_switch(HDC dc,RECT r,int on){
 int w=r.right-r.left,h=r.bottom-r.top;HDC mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,w*4,h*4),oldbmp=SelectObject(mem,bitmap);
 RECT full={0,0,w*4,h*4};ui_fill(mem,full,UI_BG);SetMapMode(mem,MM_ANISOTROPIC);SetWindowExtEx(mem,56,36,NULL);SetViewportExtEx(mem,w*4,h*4,NULL);
 HPEN outline=CreatePen(PS_SOLID,1,on?UI_RED:RGB(225,225,225));HGDIOBJ oldpen=SelectObject(mem,outline);HBRUSH track=CreateSolidBrush(on?UI_RED:UI_BG),oldbrush=SelectObject(mem,track);RoundRect(mem,2,6,54,30,24,24);SelectObject(mem,GetStockObject(NULL_PEN));
 HBRUSH knob=CreateSolidBrush(RGB(239,239,239));SelectObject(mem,knob);int x=on?35:6;Ellipse(mem,x,10,x+16,26);SelectObject(mem,oldbrush);SelectObject(mem,oldpen);DeleteObject(outline);DeleteObject(track);DeleteObject(knob);SetMapMode(mem,MM_TEXT);
 int mode=SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,r.left,r.top,w,h,mem,0,0,w*4,h*4,SRCCOPY);SetStretchBltMode(dc,mode);SelectObject(mem,oldbmp);DeleteObject(bitmap);DeleteDC(mem);
}

