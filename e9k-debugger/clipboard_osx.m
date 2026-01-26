#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

#include "clipboard.h"

int
clipboard_setPng(const void *png_data, size_t length)
{
    @autoreleasepool {
        if (!png_data || length == 0) {
            return 0;
        }
        NSData *data = [NSData dataWithBytes:png_data length:length];
        if (!data) {
            return 0;
        }
        NSImage *image = [[NSImage alloc] initWithData:data];
        if (!image || image.size.width == 0) {
            return 0;
        }
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        BOOL ok = [pb writeObjects:@[image]];
        return ok ? 1 : 0;
    }
}
