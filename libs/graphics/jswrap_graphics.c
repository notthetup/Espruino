/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2013 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * This file is designed to be parsed during the build process
 *
 * Contains JavaScript Graphics Draw Functions
 * ----------------------------------------------------------------------------
 */
#include "jswrap_graphics.h"
#include "jsutils.h"
#include "jsinteractive.h"

#include "lcd_arraybuffer.h"
#include "lcd_js.h"
#ifdef USE_LCD_SDL
#include "lcd_sdl.h"
#endif
#ifdef USE_LCD_FSMC
#include "lcd_fsmc.h"
#endif
#include "bitmap_font_4x6.h"

/*JSON{ "type":"class",
        "class" : "Graphics",
        "description" : ["This class provides Graphics operations that can be applied to a surface.",
                         "Use Graphics.createXXX to create a graphics object that renders in the way you want.",
                         "NOTE: On boards that contain an LCD, there is a built-in 'LCD' object of type Graphics. For instance to draw a line you'd type: ```LCD.drawLine(0,0,100,100)```" ]
}*/

/*JSON{ "type":"idle", "generate" : "jswrap_graphics_idle" }*/
bool jswrap_graphics_idle() {
  graphicsIdle();
  return false;
}

/*JSON{ "type":"init", "generate" : "jswrap_graphics_init" }*/
void jswrap_graphics_init() {
#ifdef USE_LCD_FSMC
  JsVar *parent = jspNewObject("LCD", "Graphics");
  if (parent) {
    JsVar *parentObj = jsvSkipName(parent);
    JsGraphics gfx;
    graphicsStructInit(&gfx);
    gfx.data.type = JSGRAPHICSTYPE_FSMC;
    gfx.graphicsVar = parentObj;
    gfx.data.width = 320;
    gfx.data.height = 240;
    gfx.data.bpp = 16;
    lcdInit_FSMC(&gfx);
    lcdSetCallbacks_FSMC(&gfx);
    graphicsSplash(&gfx);
    graphicsSetVar(&gfx);
    jsvUnLock(parentObj);
    jsvUnLock(parent);
  }
#endif
}


static bool isValidBPP(int bpp) {
  return bpp==1 || bpp==2 || bpp==4 || bpp==8 || bpp==16 || bpp==24 || bpp==32; // currently one colour can't ever be spread across multiple bytes
}

/*JSON{ "type":"staticmethod", "class": "Graphics", "name" : "createArrayBuffer",
         "description" : "Create a Graphics object that renders to an Array Buffer. This will have a field called 'buffer' that can get used to get at the buffer itself",
         "generate" : "jswrap_graphics_createArrayBuffer",
         "params" : [ [ "width", "int32", "Pixels wide" ],
                      [ "height", "int32", "Pixels high" ],
                      [ "bpp", "int32", "Number of bits per pixel" ],
                      [ "options", "JsVar", ["An object of other options. ```{ zigzag : true/false(default), vertical_byte : true/false(default) }```",
                                             "zigzag = whether to alternate the direction of scanlines for rows",
                                             "vertical_byte = whether to align bits in a byte vertically or not"] ] ],
         "return" : [ "JsVar", "The new Graphics object" ]
}*/
JsVar *jswrap_graphics_createArrayBuffer(int width, int height, int bpp, JsVar *options) {
  if (width<=0 || height<=0 || width>1023 || height>1023) {
    jsWarn("Invalid Size");
    return 0;
  }
  if (!isValidBPP(bpp)) {
    jsWarn("Invalid BPP");
    return 0;
  }

  JsVar *parent = jspNewObject(0, "Graphics");
  if (!parent) return 0; // low memory

  JsGraphics gfx;
  graphicsStructInit(&gfx);
  gfx.data.type = JSGRAPHICSTYPE_ARRAYBUFFER;
  gfx.data.flags = JSGRAPHICSFLAGS_NONE;
  gfx.graphicsVar = parent;
  gfx.data.width = (unsigned short)width;
  gfx.data.height = (unsigned short)height;
  gfx.data.bpp = (unsigned char)bpp;

  if (jsvIsObject(options)) {
    if (jsvGetBoolAndUnLock(jsvObjectGetChild(options, "zigzag", 0)))
      gfx.data.flags = (JsGraphicsFlags)(gfx.data.flags | JSGRAPHICSFLAGS_ARRAYBUFFER_ZIGZAG);
    if (jsvGetBoolAndUnLock(jsvObjectGetChild(options, "vertical_byte", 0))) {
      if (gfx.data.bpp==1)
        gfx.data.flags = (JsGraphicsFlags)(gfx.data.flags | JSGRAPHICSFLAGS_ARRAYBUFFER_VERTICAL_BYTE);
      else
        jsWarn("vertical_byte only works for 1bpp ArrayBuffers\n");
    }
  }

  lcdInit_ArrayBuffer(&gfx);
  graphicsSetVar(&gfx);
  return parent;
}

/*JSON{ "type":"staticmethod", "class": "Graphics", "name" : "createCallback",
         "description" : "Create a Graphics object that renders by calling a JavaScript callback function to draw pixels",
         "generate" : "jswrap_graphics_createCallback",
         "params" : [ [ "width", "int32", "Pixels wide" ],
                      [ "height", "int32", "Pixels high" ],
                      [ "bpp", "int32", "Number of bits per pixel" ],
                      [ "callback", "JsVar", "A function of the form ```function(x,y,col)``` that is called whenever a pixel needs to be drawn, or an object with: ```{setPixel:function(x,y,col),fillRect:function(x1,y1,x2,y2,col)}```. All arguments are already bounds checked." ] ],
         "return" : [ "JsVar", "The new Graphics object" ]
}*/
JsVar *jswrap_graphics_createCallback(int width, int height, int bpp, JsVar *callback) {
  if (width<=0 || height<=0 || width>1023 || height>1023) {
    jsWarn("Invalid Size");
    return 0;
  }
  if (!isValidBPP(bpp)) {
    jsWarn("Invalid BPP");
    return 0;
  }
  JsVar *callbackSetPixel = 0;
  JsVar *callbackFillRect = 0;
  if (jsvIsObject(callback)) {
    jsvUnLock(callbackSetPixel);
    callbackSetPixel = jsvObjectGetChild(callback, "setPixel", 0);
    callbackFillRect = jsvObjectGetChild(callback, "fillRect", 0);
  } else
    callbackSetPixel = jsvLockAgain(callback);
  if (!jsvIsFunction(callbackSetPixel)) {
    jsExceptionHere(JSET_ERROR, "Expecting Callback Function or an Object but got %t", callbackSetPixel);
    jsvUnLock(callbackSetPixel);jsvUnLock(callbackFillRect);
    return 0;
  }
  if (!jsvIsUndefined(callbackFillRect) && !jsvIsFunction(callbackFillRect)) {
    jsExceptionHere(JSET_ERROR, "Expecting Callback Function or an Object but got %t", callbackFillRect);
    jsvUnLock(callbackSetPixel);jsvUnLock(callbackFillRect);
    return 0;
  }

  JsVar *parent = jspNewObject(0, "Graphics");
  if (!parent) return 0; // low memory

  JsGraphics gfx;
  graphicsStructInit(&gfx);
  gfx.data.type = JSGRAPHICSTYPE_JS;
  gfx.graphicsVar = parent;
  gfx.data.width = (unsigned short)width;
  gfx.data.height = (unsigned short)height;
  gfx.data.bpp = (unsigned char)bpp;
  lcdInit_JS(&gfx, callbackSetPixel, callbackFillRect);
  graphicsSetVar(&gfx);
  jsvUnLock(callbackSetPixel);jsvUnLock(callbackFillRect);
  return parent;
}

#ifdef USE_LCD_SDL
/*JSON{ "type":"staticmethod", "class": "Graphics", "name" : "createSDL", "ifdef" : "USE_LCD_SDL",
         "description" : "Create a Graphics object that renders to SDL window (Linux-based devices only)",
         "generate" : "jswrap_graphics_createSDL",
         "params" : [ [ "width", "int32", "Pixels wide" ],
                      [ "height", "int32", "Pixels high" ] ],
         "return" : [ "JsVar", "The new Graphics object" ]
}*/
JsVar *jswrap_graphics_createSDL(int width, int height) {
  if (width<=0 || height<=0 || width>1023 || height>1023) {
    jsWarn("Invalid Size");
    return 0;
  }

  JsVar *parent = jspNewObject(0, "Graphics");
  if (!parent) return 0; // low memory
  JsGraphics gfx;
  graphicsStructInit(&gfx);
  gfx.data.type = JSGRAPHICSTYPE_SDL;
  gfx.graphicsVar = parent;
  gfx.data.width = (unsigned short)width;
  gfx.data.height = (unsigned short)height;
  gfx.data.bpp = 32;
  lcdInit_SDL(&gfx);
  graphicsSetVar(&gfx);
  return parent;
}
#endif

/*JSON{ "type":"method", "class": "Graphics", "name" : "getWidth",
         "description" : "The width of the LCD",
         "generate_full" : "jswrap_graphics_getWidthOrHeight(parent, false)",
         "return" : [ "int", "The width of the LCD" ]
}*/
/*JSON{ "type":"method", "class": "Graphics", "name" : "getHeight",
         "description" : "The height of the LCD",
         "generate_full" : "jswrap_graphics_getWidthOrHeight(parent, true)",
         "return" : [ "int", "The height of the LCD" ]
}*/
int jswrap_graphics_getWidthOrHeight(JsVar *parent, bool height) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return 0;
  if (gfx.data.flags & JSGRAPHICSFLAGS_SWAP_XY)
    height=!height;
  return height ? gfx.data.height : gfx.data.width;
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "clear",
         "description" : "Clear the LCD with the Background Color",
         "generate" : "jswrap_graphics_clear"
}*/
void jswrap_graphics_clear(JsVar *parent) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;
  graphicsClear(&gfx);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "fillRect",
         "description" : "Fill a rectangular area in the Foreground Color",
         "generate" : "jswrap_graphics_fillRect",
         "params" : [ [ "x1", "int32", "The left" ],
                      [ "y1", "int32", "The top" ],
                      [ "x2", "int32", "The right" ],
                      [ "y2", "int32", "The bottom" ] ]
}*/
void jswrap_graphics_fillRect(JsVar *parent, int x1, int y1, int x2, int y2) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;
  graphicsFillRect(&gfx, (short)x1,(short)y1,(short)x2,(short)y2);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "drawRect",
         "description" : "Draw an unfilled rectangle 1px wide in the Foreground Color",
         "generate" : "jswrap_graphics_drawRect",
         "params" : [ [ "x1", "int32", "The left" ],
                      [ "y1", "int32", "The top" ],
                      [ "x2", "int32", "The right" ],
                      [ "y2", "int32", "The bottom" ]]
}*/
void jswrap_graphics_drawRect(JsVar *parent, int x1, int y1, int x2, int y2) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;
  graphicsDrawRect(&gfx, (short)x1,(short)y1,(short)x2,(short)y2);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "getPixel",
         "description" : "Get a pixel's color",
         "generate" : "jswrap_graphics_getPixel",
         "params" : [ [ "x", "int32", "The left" ],
                      [ "y", "int32", "The top" ] ],
         "return" : [ "int32", "The color" ]
}*/
int jswrap_graphics_getPixel(JsVar *parent, int x, int y) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return 0;
  return (int)graphicsGetPixel(&gfx, (short)x, (short)y);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "setPixel",
         "description" : "Set a pixel's color",
         "generate" : "jswrap_graphics_setPixel",
         "params" : [ [ "x", "int32", "The left" ],
                      [ "y", "int32", "The top" ],
                      [ "col", "JsVar", "The color" ] ]
}*/
void jswrap_graphics_setPixel(JsVar *parent, int x, int y, JsVar *color) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;
  unsigned int col = gfx.data.fgColor;
  if (!jsvIsUndefined(color)) 
    col = (unsigned int)jsvGetInteger(color);
  graphicsSetPixel(&gfx, (short)x, (short)y, col);
  gfx.data.cursorX = (short)x;
  gfx.data.cursorY = (short)y;
}


/*JSON{ "type":"method", "class": "Graphics", "name" : "setColor",
         "description" : "Set the color to use for subsequent drawing operations",
         "generate_full" : "jswrap_graphics_setColorX(parent, r,g,b, true)",
         "params" : [ [ "r", "JsVar", "Red (between 0 and 1) OR an integer representing the color in the current bit depth" ],
                      [ "g", "JsVar", "Green (between 0 and 1)" ],
                      [ "b", "JsVar", "Blue (between 0 and 1)" ] ]
}*/
/*JSON{ "type":"method", "class": "Graphics", "name" : "setBgColor",
         "description" : "Set the background color to use for subsequent drawing operations",
         "generate_full" : "jswrap_graphics_setColorX(parent, r,g,b, false)",
         "params" : [ [ "r", "JsVar", "Red (between 0 and 1) OR an integer representing the color in the current bit depth" ],
                      [ "g", "JsVar", "Green (between 0 and 1)" ],
                      [ "b", "JsVar", "Blue (between 0 and 1)" ] ]
}*/
void jswrap_graphics_setColorX(JsVar *parent, JsVar *r, JsVar *g, JsVar *b, bool isForeground) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;
  unsigned int color = 0;
  if (!jsvIsUndefined(g) && !jsvIsUndefined(b)) {
    int ri = (int)(jsvGetFloat(r)*256);
    int gi = (int)(jsvGetFloat(g)*256);
    int bi = (int)(jsvGetFloat(b)*256);
    if (ri>255) ri=255;
    if (gi>255) gi=255;
    if (bi>255) bi=255;
    if (ri<0) ri=0;
    if (gi<0) gi=0;
    if (bi<0) bi=0;
    if (gfx.data.bpp==16) {
      color = (unsigned int)((bi>>3) | (gi>>2)<<5 | (ri>>3)<<11);
    } else if (gfx.data.bpp==32) {
      color = 0xFF000000 | (unsigned int)(bi | (gi<<8) | (ri<<16));
    } else if (gfx.data.bpp==24) {
      color = (unsigned int)(bi | (gi<<8) | (ri<<16));
    } else
      color = (unsigned int)(((ri+gi+bi)>=384) ? 0xFFFFFFFF : 0);
  } else {
    // just rgb
    color = (unsigned int)jsvGetInteger(r);
  }
  if (isForeground)
    gfx.data.fgColor = color;
  else
    gfx.data.bgColor = color;
  graphicsSetVar(&gfx);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "getColor",
         "description" : "Get the color to use for subsequent drawing operations",
         "generate_full" : "jswrap_graphics_getColorX(parent, true)",
         "return" : [ "int", "The integer value of the colour" ]
}*/
/*JSON{ "type":"method", "class": "Graphics", "name" : "getBgColor",
         "description" : "Get the background color to use for subsequent drawing operations",
         "generate_full" : "jswrap_graphics_getColorX(parent, false)",
         "return" : [ "int", "The integer value of the colour" ]
}*/
JsVarInt jswrap_graphics_getColorX(JsVar *parent, bool isForeground) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return 0;
  return (JsVarInt)((isForeground ? gfx.data.fgColor : gfx.data.bgColor) & ((1L<<gfx.data.bpp)-1));
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "setFontBitmap",
         "description" : "Set Graphics to draw with a Bitmapped Font",
         "generate_full" : "jswrap_graphics_setFontSizeX(parent, JSGRAPHICS_FONTSIZE_4X6, false)"
}*/
/*JSON{ "type":"method", "class": "Graphics", "name" : "setFontVector", "ifndef" : "SAVE_ON_FLASH",
         "description" : "Set Graphics to draw with a Vector Font of the given size",
         "generate_full" : "jswrap_graphics_setFontSizeX(parent, size, true)",
         "params" : [ [ "size", "int32", "The size as an integer" ] ]
}*/
void jswrap_graphics_setFontSizeX(JsVar *parent, int size, bool checkValid) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;

  if (checkValid) {
    if (size<1) size=1;
    if (size>1023) size=1023;
  }
  if (gfx.data.fontSize == JSGRAPHICS_FONTSIZE_CUSTOM) {
    jsvObjectSetChild(parent, JSGRAPHICS_CUSTOMFONT_BMP, 0);
    jsvObjectSetChild(parent, JSGRAPHICS_CUSTOMFONT_WIDTH, 0);
    jsvObjectSetChild(parent, JSGRAPHICS_CUSTOMFONT_HEIGHT, 0);
    jsvObjectSetChild(parent, JSGRAPHICS_CUSTOMFONT_FIRSTCHAR, 0);
  }
  gfx.data.fontSize = (short)size;
  graphicsSetVar(&gfx);
}
/*JSON{ "type":"method", "class": "Graphics", "name" : "setFontCustom",
         "description" : "Set Graphics to draw with a Custom Font",
         "generate" : "jswrap_graphics_setFontCustom",
         "params" : [ [ "bitmap", "JsVar", "A column-first, MSB-first, 1bpp bitmap containing the font bitmap" ],
                      [ "firstChar", "int32", "The first character in the font - usually 32 (space)" ],
                      [ "width", "JsVar", "The width of each character in the font. Either an integer, or a string where each character represents the width" ],
                      [ "height", "int32", "The height as an integer" ] ]
}*/
void jswrap_graphics_setFontCustom(JsVar *parent, JsVar *bitmap, int firstChar, JsVar *width, int height) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;

  if (!jsvIsString(bitmap)) {
    jsExceptionHere(JSET_ERROR, "Font bitmap must be a String");
    return;
  }
  if (firstChar<0 || firstChar>255) {
    jsExceptionHere(JSET_ERROR, "First character out of range");
    return;
  }
  if (!jsvIsString(width) && !jsvIsInt(width)) {
    jsExceptionHere(JSET_ERROR, "Font width must be a String or an integer");
    return;
  }
  if (height<=0 || height>255) {
   jsExceptionHere(JSET_ERROR, "Invalid height");
   return;
 }
  jsvObjectSetChild(parent, JSGRAPHICS_CUSTOMFONT_BMP, bitmap);
  jsvObjectSetChild(parent, JSGRAPHICS_CUSTOMFONT_WIDTH, width);
  jsvUnLock(jsvObjectSetChild(parent, JSGRAPHICS_CUSTOMFONT_HEIGHT, jsvNewFromInteger(height)));
  jsvUnLock(jsvObjectSetChild(parent, JSGRAPHICS_CUSTOMFONT_FIRSTCHAR, jsvNewFromInteger(firstChar)));
  gfx.data.fontSize = JSGRAPHICS_FONTSIZE_CUSTOM;
  graphicsSetVar(&gfx);
}


/*JSON{ "type":"method", "class": "Graphics", "name" : "drawString",
         "description" : "Draw a string of text in the current font",
         "generate" : "jswrap_graphics_drawString",
         "params" : [ [ "str", "JsVar", "The string" ],
                      [ "x", "int32", "The left" ],
                      [ "y", "int32", "The top" ] ]
}*/
void jswrap_graphics_drawString(JsVar *parent, JsVar *var, int x, int y) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;

  JsVar *customBitmap = 0, *customWidth = 0;
  int customHeight, customFirstChar;
  if (gfx.data.fontSize == JSGRAPHICS_FONTSIZE_CUSTOM) {
    customBitmap = jsvObjectGetChild(parent, JSGRAPHICS_CUSTOMFONT_BMP, 0);
    customWidth = jsvObjectGetChild(parent, JSGRAPHICS_CUSTOMFONT_WIDTH, 0);
    customHeight = (int)jsvGetIntegerAndUnLock(jsvObjectGetChild(parent, JSGRAPHICS_CUSTOMFONT_HEIGHT, 0));
    customFirstChar = (int)jsvGetIntegerAndUnLock(jsvObjectGetChild(parent, JSGRAPHICS_CUSTOMFONT_FIRSTCHAR, 0));
  }

  JsVar *str = jsvAsString(var, false);
  JsvStringIterator it;
  jsvStringIteratorNew(&it, str, 0);
  while (jsvStringIteratorHasChar(&it)) {
    char ch = jsvStringIteratorGetChar(&it);
    if (gfx.data.fontSize>0) {
#ifndef SAVE_ON_FLASH
      int w = (int)graphicsFillVectorChar(&gfx, (short)x, (short)y, gfx.data.fontSize, ch);
      x+=w;
#endif
    } else if (gfx.data.fontSize == JSGRAPHICS_FONTSIZE_4X6) {
      graphicsDrawChar4x6(&gfx, (short)x, (short)y, ch);
      x+=4;
    } else if (gfx.data.fontSize == JSGRAPHICS_FONTSIZE_CUSTOM) {
      // get char width and offset in string
      int width = 0, bmpOffset = 0;
      if (jsvIsString(customWidth)) {
        if (ch>=customFirstChar) {
          JsvStringIterator wit;
          jsvStringIteratorNew(&wit, customWidth, 0);
          while (jsvStringIteratorHasChar(&wit) && (int)jsvStringIteratorGetIndex(&wit)<(ch-customFirstChar)) {
            bmpOffset += (unsigned char)jsvStringIteratorGetChar(&wit);
            jsvStringIteratorNext(&wit);
          }
          width = (unsigned char)jsvStringIteratorGetChar(&wit);
          jsvStringIteratorFree(&wit);
        }
      } else {
        width = (int)jsvGetInteger(customWidth);
        bmpOffset = width*(ch-customFirstChar);
      }
      if (ch>=customFirstChar) {
        bmpOffset *= customHeight;
        // now render character
        JsvStringIterator cit;
        jsvStringIteratorNew(&cit, customBitmap, (size_t)bmpOffset>>3);
        bmpOffset &= 7;
        int cx,cy;
        for (cx=0;cx<width;cx++) {
          for (cy=0;cy<customHeight;cy++) {
            if ((jsvStringIteratorGetChar(&cit)<<bmpOffset)&128)
              graphicsSetPixel(&gfx, (short)(cx+x), (short)(cy+y), gfx.data.fgColor);
            bmpOffset++;
            if (bmpOffset==8) {
              bmpOffset=0;
              jsvStringIteratorNext(&cit);
            }
          }
        }
        jsvStringIteratorFree(&cit);
      }
      x += width;
    }
    if (jspIsInterrupted()) break;
    jsvStringIteratorNext(&it);
  }
  jsvStringIteratorFree(&it);
  jsvUnLock(str);

  jsvUnLock(customBitmap);
  jsvUnLock(customWidth);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "stringWidth",
         "description" : "Return the size in pixels of a string of text in the current font",
         "generate" : "jswrap_graphics_stringWidth",
         "params" : [ [ "str", "JsVar", "The string" ] ],
         "return" : [ "int", "The length of the string in pixels" ]
}*/
JsVarInt jswrap_graphics_stringWidth(JsVar *parent, JsVar *var) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return 0;

  JsVar *customWidth = 0;
  int customFirstChar;
  if (gfx.data.fontSize == JSGRAPHICS_FONTSIZE_CUSTOM) {
    customWidth = jsvObjectGetChild(parent, JSGRAPHICS_CUSTOMFONT_WIDTH, 0);
    customFirstChar = (int)jsvGetIntegerAndUnLock(jsvObjectGetChild(parent, JSGRAPHICS_CUSTOMFONT_FIRSTCHAR, 0));
  }

  JsVar *str = jsvAsString(var, false);
  JsvStringIterator it;
  jsvStringIteratorNew(&it, str, 0);
  int width = 0;
  while (jsvStringIteratorHasChar(&it)) {
    char ch = jsvStringIteratorGetChar(&it);
    if (gfx.data.fontSize>0) {
#ifndef SAVE_ON_FLASH
      width += (int)graphicsVectorCharWidth(&gfx, gfx.data.fontSize, ch);
#endif
    } else if (gfx.data.fontSize == JSGRAPHICS_FONTSIZE_4X6) {
      width += 4;
    } else if (gfx.data.fontSize == JSGRAPHICS_FONTSIZE_CUSTOM) {
      if (jsvIsString(customWidth)) {
        if (ch>=customFirstChar)
          width += (unsigned char)jsvGetCharInString(customWidth, (size_t)(ch-customFirstChar));
      } else
        width += (int)jsvGetInteger(customWidth);
    }
    jsvStringIteratorNext(&it);
  }
  jsvStringIteratorFree(&it);
  jsvUnLock(str);

  jsvUnLock(customWidth);
  return width;
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "drawLine",
         "description" : "Draw a line between x1,y1 and x2,y2 in the current foreground color",
         "generate" : "jswrap_graphics_drawLine",
         "params" : [ [ "x1", "int32", "The left" ],
                      [ "y1", "int32", "The top" ],
                      [ "x2", "int32", "The right" ],
                      [ "y2", "int32", "The bottom" ] ]
}*/
void jswrap_graphics_drawLine(JsVar *parent, int x1, int y1, int x2, int y2) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;
  graphicsDrawLine(&gfx, (short)x1,(short)y1,(short)x2,(short)y2);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "lineTo",
         "description" : "Draw a line from the last position of lineTo or moveTo to this position",
         "generate" : "jswrap_graphics_lineTo",
         "params" : [ [ "x", "int32", "X value" ],
                      [ "y", "int32", "Y value" ] ]
}*/
void jswrap_graphics_lineTo(JsVar *parent, int x, int y) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;
  graphicsDrawLine(&gfx, gfx.data.cursorX, gfx.data.cursorY, (short)x, (short)y);
  gfx.data.cursorX = (short)x;
  gfx.data.cursorY = (short)y;
  graphicsSetVar(&gfx);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "moveTo",
         "description" : "Move the cursor to a position - see lineTo",
         "generate" : "jswrap_graphics_moveTo",
         "params" : [ [ "x", "int32", "X value" ],
                      [ "y", "int32", "Y value" ] ]
}*/
void jswrap_graphics_moveTo(JsVar *parent, int x, int y) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;
  gfx.data.cursorX = (short)x;
  gfx.data.cursorY = (short)y;
  graphicsSetVar(&gfx);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "fillPoly",
         "description" : "Draw a filled polygon in the current foreground color",
         "generate" : "jswrap_graphics_fillPoly",
         "params" : [ [ "poly", "JsVar", "An array of vertices, of the form ```[x1,y1,x2,y2,x3,y3,etc]```" ] ]
}*/
void jswrap_graphics_fillPoly(JsVar *parent, JsVar *poly) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;
  if (!jsvIsArray(poly)) return;
  const int maxVerts = 128;
  short verts[maxVerts];
  int idx = 0;
  JsVarRef item = poly->firstChild;
  while (item && idx<maxVerts) {
    JsVar *val = jsvLock(item);
    verts[idx++] = (short)jsvGetIntegerAndUnLock(jsvSkipName(val));
    item = val->nextSibling;
    jsvUnLock(val);
  }
  if (idx==maxVerts) {
    jsWarn("Maximum number of points (%d) exceeded for fillPoly", maxVerts/2);
  }
  graphicsFillPoly(&gfx, idx/2, verts);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "setRotation",
         "description" : "Set the current rotation of the graphics device.",
         "generate" : "jswrap_graphics_setRotation",
         "params" : [ [ "rotation", "int32", "The clockwise rotation. 0 for no rotation, 1 for 90 degrees, 2 for 180, 3 for 270" ],
                      [ "reflect", "bool", "Whether to reflect the image" ] ]
}*/
void jswrap_graphics_setRotation(JsVar *parent, int rotation, bool reflect) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;

  // clear flags
  gfx.data.flags &= (JsGraphicsFlags)~(JSGRAPHICSFLAGS_SWAP_XY | JSGRAPHICSFLAGS_INVERT_X | JSGRAPHICSFLAGS_INVERT_Y);
  // set flags
  switch (rotation) {
    case 0:
      break;
    case 1:
      gfx.data.flags |= JSGRAPHICSFLAGS_SWAP_XY | JSGRAPHICSFLAGS_INVERT_X;
      break;
    case 2:
      gfx.data.flags |= JSGRAPHICSFLAGS_INVERT_X | JSGRAPHICSFLAGS_INVERT_Y;
      break;
    case 3:
      gfx.data.flags |= JSGRAPHICSFLAGS_SWAP_XY | JSGRAPHICSFLAGS_INVERT_Y;
      break;
  }

  if (reflect) {
    if (gfx.data.flags & JSGRAPHICSFLAGS_SWAP_XY)
      gfx.data.flags ^= JSGRAPHICSFLAGS_INVERT_Y;
    else
      gfx.data.flags ^= JSGRAPHICSFLAGS_INVERT_X;
  }

  graphicsSetVar(&gfx);
}

/*JSON{ "type":"method", "class": "Graphics", "name" : "drawImage",
         "description" : "Draw an image at the specified position. If the image is 1 bit, the graphics foreground/background colours will be used. Otherwise color data will be copied as-is. Bitmaps are rendered MSB-first",
         "generate" : "jswrap_graphics_drawImage",
         "params" : [ [ "image", "JsVar", "An object with the following fields `{ width : int, height : int, bpp : int, buffer : ArrayBuffer, transparent: optional int }`. bpp = bits per pixel, transparent (if defined) is the colour that will be treated as transparent" ],
                      [ "x", "int32", "The X offset to draw the image" ],
                      [ "y", "int32", "The Y offset to draw the image" ] ]
}*/
void jswrap_graphics_drawImage(JsVar *parent, JsVar *image, int xPos, int yPos) {
  JsGraphics gfx; if (!graphicsGetFromVar(&gfx, parent)) return;
  if (!jsvIsObject(image)) {
    jsExceptionHere(JSET_ERROR, "Expecting first argument to be an object");
    return;
  }
  int imageWidth = (int)jsvGetIntegerAndUnLock(jsvObjectGetChild(image, "width", 0));
  int imageHeight = (int)jsvGetIntegerAndUnLock(jsvObjectGetChild(image, "height", 0));
  int imageBpp = (int)jsvGetIntegerAndUnLock(jsvObjectGetChild(image, "bpp", 0));
  unsigned int imageBitMask = (unsigned int)((1L<<imageBpp)-1L);
  JsVar *transpVar = jsvObjectGetChild(image, "transparent", 0);
  bool imageIsTransparent = transpVar!=0;
  unsigned int imageTransparentCol = (unsigned int)jsvGetInteger(transpVar);
  jsvUnLock(transpVar);
  JsVar *imageBuffer = jsvObjectGetChild(image, "buffer", 0);
  if (!(jsvIsArrayBuffer(imageBuffer) && imageWidth>0 && imageHeight>0 && imageBpp>0 && imageBpp<=32)) {
    jsExceptionHere(JSET_ERROR, "Expecting first argument to a valid Image");
    jsvUnLock(imageBuffer);
    return;
  }
  JsVar *imageBufferString = jsvGetArrayBufferBackingString(imageBuffer);
  jsvUnLock(imageBuffer);


  int x=0, y=0;
  int bits=0;
  unsigned int colData = 0;
  JsvStringIterator it;
  jsvStringIteratorNew(&it, imageBufferString, 0);
  while ((bits>=imageBpp || jsvStringIteratorHasChar(&it)) && y<imageHeight) {
    // Get the data we need...
    while (bits < imageBpp) {
      colData = (colData<<8) | ((unsigned char)jsvStringIteratorGetChar(&it));
      jsvStringIteratorNext(&it);
      bits += 8;
    }
    // extract just the bits we want
    unsigned int col = (colData>>(bits-imageBpp))&imageBitMask;
    bits -= imageBpp;
    // Try and write pixel!
    if (!imageIsTransparent || imageTransparentCol!=col) {
      if (imageBpp==1)
        col = col ? gfx.data.fgColor : gfx.data.bgColor;
      graphicsSetPixel(&gfx, (short)(x+xPos), (short)(y+yPos), col);
    }
    // Go to next pixel
    x++;
    if (x>=imageWidth) {
      x=0;
      y++;
      // we don't care about image height - we'll stop next time...
    }

  }
  jsvStringIteratorFree(&it);
  jsvUnLock(imageBufferString);
}
